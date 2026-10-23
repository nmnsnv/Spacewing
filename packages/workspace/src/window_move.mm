#import "window_move.h"
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#import <mach-o/nlist.h>

// Private Dock-managed operation, available with SIP enabled.
// Resolve at runtime; missing window-move support must not disable plain switching.
@protocol BFManagedSpaceMove
- (instancetype)initWithWindows:(NSArray<NSNumber *> *)windows spaceID:(uint64_t)space;
@end

@protocol BFSpaceMembershipMove
- (instancetype)initWithSpaceID:(uint64_t)space
                        windows:(NSArray<NSNumber *> *)windows
                        options:(uint32_t)options;
@end

typedef int64_t (*BFPerformOperation)(void *);

// The dispatcher is a local C++ symbol, not a dlsym-exported C function.
// This inspects SkyLight already loaded in OUR process; it injects no code.
static void *findLocalSkyLightSymbol(const char *name) {
    const char *path = "/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight";
    for (uint32_t image = 0; image < _dyld_image_count(); ++image) {
        const char *imageName = _dyld_get_image_name(image);
        if (!imageName || strcmp(imageName, path) != 0)
            continue;
        const struct mach_header_64 *header =
            (const struct mach_header_64 *)_dyld_get_image_header(image);
        if (!header || header->magic != MH_MAGIC_64)
            return NULL;
        const uint8_t *cursor = (const uint8_t *)(header + 1);
        const uint8_t *end = cursor + header->sizeofcmds;
        const struct segment_command_64 *linkedit = NULL;
        const struct symtab_command *symbols = NULL;
        for (uint32_t command = 0; command < header->ncmds; ++command) {
            if ((size_t)(end - cursor) < sizeof(struct load_command))
                return NULL;
            const struct load_command *load = (const struct load_command *)cursor;
            if (load->cmdsize < sizeof(*load) || load->cmdsize > (size_t)(end - cursor))
                return NULL;
            if (load->cmd == LC_SEGMENT_64 && load->cmdsize >= sizeof(struct segment_command_64)) {
                const struct segment_command_64 *segment = (const struct segment_command_64 *)load;
                if (strncmp(segment->segname, SEG_LINKEDIT, sizeof(segment->segname)) == 0)
                    linkedit = segment;
            } else if (load->cmd == LC_SYMTAB && load->cmdsize >= sizeof(struct symtab_command)) {
                symbols = (const struct symtab_command *)load;
            }
            cursor += load->cmdsize;
        }
        if (!linkedit || !symbols)
            return NULL;
        uint64_t fileEnd = linkedit->fileoff + linkedit->filesize;
        if (fileEnd < linkedit->fileoff || symbols->symoff < linkedit->fileoff ||
            symbols->stroff < linkedit->fileoff ||
            (uint64_t)symbols->symoff + (uint64_t)symbols->nsyms * sizeof(struct nlist_64) >
                fileEnd ||
            (uint64_t)symbols->stroff + symbols->strsize > fileEnd)
            return NULL;
        intptr_t slide = _dyld_get_image_vmaddr_slide(image);
        uintptr_t base = linkedit->vmaddr + slide - linkedit->fileoff;
        const struct nlist_64 *table = (const struct nlist_64 *)(base + symbols->symoff);
        const char *strings = (const char *)(base + symbols->stroff);
        size_t length = strlen(name);
        for (uint32_t index = 0; index < symbols->nsyms; ++index) {
            const struct nlist_64 *symbol = &table[index];
            uint32_t offset = symbol->n_un.n_strx;
            if (offset >= symbols->strsize || length >= symbols->strsize - offset ||
                (symbol->n_type & N_STAB) || (symbol->n_type & N_TYPE) != N_SECT)
                continue;
            if (memcmp(strings + offset, name, length + 1) == 0)
                return (void *)(symbol->n_value + slide);
        }
        return NULL;
    }
    return NULL;
}

static BFPerformOperation performer(void) {
    static BFPerformOperation perform;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      perform = (BFPerformOperation)dlsym(RTLD_DEFAULT,
                                          "SLSPerformAsynchronousBridgedWindowManagementOperation");
      if (!perform)
          perform = (BFPerformOperation)findLocalSkyLightSymbol(
              "__"
              "ZL54SLSPerformAsynchronousBridgedWindowManagementOperationP47SLSAsynchronousBridgedW"
              "indowManagementOperation");
    });
    return perform;
}

static Class operationClass(bool fullscreenTarget) {
    return NSClassFromString(fullscreenTarget
                                 ? @"SLSBridgedSpaceAddWindowsAndRemoveFromSpacesOperation"
                                 : @"SLSBridgedMoveWindowsToManagedSpaceOperation");
}

bool BFCanMoveWindows(bool fullscreenTarget) {
    SEL initializer = fullscreenTarget ? @selector(initWithSpaceID:windows:options:)
                                       : @selector(initWithWindows:spaceID:);
    return performer() != NULL &&
           [operationClass(fullscreenTarget) instancesRespondToSelector:initializer];
}

bool BFSubmitWindowMove(uint32_t windowID, uint64_t spaceID, bool fullscreenTarget) {
    @autoreleasepool {
        if (!BFCanMoveWindows(fullscreenTarget))
            return false;
        @try {
            id operation;
            if (fullscreenTarget) {
                // The managed move refuses ordinary app windows on fullscreen Spaces.
                // Add the window directly, removing membership from all other Space
                // types (mask 0x7). A zero mask leaves it on the source desktop too.
                operation = [(id<BFSpaceMembershipMove>)[operationClass(true) alloc]
                    initWithSpaceID:spaceID
                            windows:@[ @(windowID) ]
                            options:0x7];
            } else {
                operation = [(id<BFManagedSpaceMove>)[operationClass(false) alloc]
                    initWithWindows:@[ @(windowID) ]
                            spaceID:spaceID];
            }
            if (!operation)
                return false;
            // The private return value is undocumented; observed Space membership
            // is the completion contract, rather than guessing its meaning.
            performer()((__bridge void *)operation);
            return true;
        } @catch (NSException *exception) {
            return false;
        }
    }
}
