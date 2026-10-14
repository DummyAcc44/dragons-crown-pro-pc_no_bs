/* libSceFont / libSceFontFt on FreeType, adapted from gr2_pc's runtime_gr2_font.c.
 * Units: scales are pixels per em (FreeType char size at 72 dpi); metrics are float pixels. */
#define _GNU_SOURCE
#include "runtime.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#define FONT_OK 0
#define ERR_FATAL ((int32_t)0x80460001)
#define ERR_INVALID_PARAMETER ((int32_t)0x80460002)
#define ERR_INVALID_MEMORY ((int32_t)0x80460003)
#define ERR_INVALID_LIBRARY ((int32_t)0x80460004)
#define ERR_INVALID_FONT_HANDLE ((int32_t)0x80460005)
#define ERR_INVALID_RENDERER ((int32_t)0x80460007)
#define ERR_ALLOCATION_FAILED ((int32_t)0x80460010)
#define ERR_NO_SUPPORT_LIBRARY ((int32_t)0x80460018)
#define ERR_FONT_OPEN_FAILED ((int32_t)0x80460036)
#define ERR_NO_SUPPORT_CODE ((int32_t)0x80460041)
#define ERR_NO_SUPPORT_GLYPH ((int32_t)0x80460042)
#define ERR_NO_SUPPORT_SURFACE ((int32_t)0x80460050)
#define ERR_ALREADY_BOUND_RENDERER ((int32_t)0x80460060)
#define ERR_NOT_BOUND_RENDERER ((int32_t)0x80460061)

#define MEM_MAGIC 0x0F00
#define LIBRARY_MAGIC 0x0F01
#define FONT_MAGIC 0x0F02
#define RENDERER_MAGIC 0x0F07

/* ---- Guest structures ---- */
/* SceFontMemory (sceFontMemoryInit fills it; the game allocates 64 bytes). */
typedef struct {
    uint16_t kind, attributes;
    uint32_t region_size;
    void *region, *mspace;
    const void *interface;
    void *destroy_callback, *destroy_object;
    void *reserved, *parent;
} FontMemory;
typedef struct { float width, height, h_bearing_x, h_bearing_y, h_advance, v_bearing_x, v_bearing_y, v_advance; } GlyphMetrics;
typedef struct { float baseline_offset, line_advance, decoration_extent; } HorizontalLayout;
typedef struct { float offset_x, offset_y, position_x, position_y; } Kerning;
typedef struct {
    void *buffer;
    int32_t width_byte;
    int8_t pixel_size_byte;
    uint8_t pad0, style_flag, pad2;
    int32_t width, height;
    uint32_t scissor_x0, scissor_y0, scissor_x1, scissor_y1;
    uint64_t reserved[11];
} RenderSurface;
typedef struct {
    const void *stage;
    struct { uint8_t *address; uint32_t width_byte; uint8_t pixel_size_byte, pixel_format; uint16_t pad; } image;
    struct { uint32_t x, y, w, h; } update;
    struct { float bearing_x, bearing_y, advance, stride; uint32_t width, height; } metrics;
} RenderOutput;
typedef struct { uint16_t tag, pad; uint32_t flags, subfont_index; int32_t unique_id; const void *reserved[2]; } OpenParams;
/* What sceFontSelectLibraryFt / sceFontSelectRendererFt return: opaque driver tables the
 * game hands back to the Create functions unchanged. */
typedef struct { uint32_t magic, reserved; void *reserved_ptr; uintptr_t functions[4]; } LibrarySelection;
typedef struct { uint32_t magic, size; uintptr_t functions[3]; } RendererSelection;
_Static_assert(sizeof(FontMemory)==64,"SceFontMemory");
_Static_assert(sizeof(GlyphMetrics)==32,"SceFontGlyphMetrics");
_Static_assert(sizeof(HorizontalLayout)==12,"SceFontHorizontalLayout");
_Static_assert(sizeof(Kerning)==16,"SceFontKerning");
_Static_assert(sizeof(RenderSurface)==128,"SceFontRenderSurface");
_Static_assert(sizeof(RenderOutput)==64,"SceFontRenderOutput");
_Static_assert(sizeof(OpenParams)==32,"SceFontOpenDetail");

/* ---- Host objects behind the guest handles ---- */
typedef struct { uint16_t magic; const FontMemory *memory; uint64_t edition; int fonts; } Library;
typedef struct { uint16_t magic; const FontMemory *memory; uint64_t edition; } Renderer;
typedef struct {
    uint16_t magic;
    Library *library;
    Renderer *renderer;
    FT_Face face;
    float scale_w, scale_h;          /* sceFontSetScalePixel: metrics and layout */
    float render_w, render_h;        /* sceFontSetupRenderScalePixel: rendering */
    FT_F26Dot6 size_w, size_h;       /* char size currently set on the face */
    uint8_t guest_reserved[256];     /* the PS4 handle is a 256-byte object */
} Font;

static const LibrarySelection library_ft={0x53464C46u /* "FLFS" */,0,NULL,{0,0,0,0}};
static const RendererSelection renderer_ft={0x53524646u /* "FFRS" */,sizeof(RendererSelection),{0,0,0}};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static FT_Library freetype; /* created with the first library, kept for the process lifetime */

static void once(const char *what) {
    static const char *seen[48];
    static int count;
    static pthread_mutex_t seen_lock=PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&seen_lock);
    for (int i=0;i<count;++i) if (seen[i]==what) { pthread_mutex_unlock(&seen_lock); return; }
    if (count<48) seen[count++]=what;
    pthread_mutex_unlock(&seen_lock);
    printf("[DEBUG_SAULO][Font] %s\n",what);
}

static Font *font_of(void *handle) {
    Font *font=handle;
    return font && font->magic==FONT_MAGIC && font->face ? font : NULL;
}

/* Char size in 26.6 at 72 dpi, so one em is `w` x `h` pixels. Called with the lock held. */
static int set_size(Font *font, float w, float h) {
    if (!(w>0.0f) || !(h>0.0f) || w>4096.0f || h>4096.0f) return -1;
    FT_F26Dot6 cw=(FT_F26Dot6)lrintf(w*64.0f), ch=(FT_F26Dot6)lrintf(h*64.0f);
    if (cw==font->size_w && ch==font->size_h) return 0;
    if (FT_Set_Char_Size(font->face,cw,ch,72,72)) { font->size_w=font->size_h=0; return -1; }
    font->size_w=cw; font->size_h=ch;
    return 0;
}

/* ---- Memory, library, renderer ---- */
static ABI int32_t font_memory_init(FontMemory *memory, void *region, uint32_t size, const void *interface,
                                    void *mspace, void *destroy_callback, void *destroy_object) {
    once("sceFontMemoryInit");
    if (!memory) return ERR_INVALID_PARAMETER;
    if (!interface && (!region || !size)) { memory->kind=0; return ERR_INVALID_PARAMETER; }
    memory->kind=MEM_MAGIC; memory->attributes=0; memory->region_size=size; memory->region=region;
    memory->mspace=mspace; memory->interface=interface; memory->destroy_callback=destroy_callback;
    memory->destroy_object=destroy_object; memory->reserved=NULL; memory->parent=mspace;
    return FONT_OK;
}

static ABI int32_t font_memory_term(FontMemory *memory) {
    once("sceFontMemoryTerm");
    if (!memory) return ERR_INVALID_PARAMETER;
    if (memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (memory->destroy_callback) {
        typedef void (ABI *Destroy)(FontMemory *, void *, void *);
        Destroy destroy=(Destroy)memory->destroy_callback;
        memory->kind=0;
        destroy(memory,memory->mspace,memory->destroy_object);
        return FONT_OK;
    }
    memset(memory,0,sizeof(*memory));
    return FONT_OK;
}

static ABI const LibrarySelection *font_select_library_ft(int32_t value) {
    (void)value; once("sceFontSelectLibraryFt"); return &library_ft;
}
static ABI const RendererSelection *font_select_renderer_ft(int32_t value) {
    (void)value; once("sceFontSelectRendererFt"); return &renderer_ft;
}

static ABI int32_t font_create_library(const FontMemory *memory, const void *selection, uint64_t edition, Library **out) {
    once("sceFontCreateLibraryWithEdition");
    if (!out) return ERR_INVALID_PARAMETER;
    *out=NULL;
    if (!memory || memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (!selection) return ERR_NO_SUPPORT_LIBRARY;
    Library *library=calloc(1,sizeof(*library));
    if (!library) return ERR_ALLOCATION_FAILED;
    pthread_mutex_lock(&lock);
    if (!freetype && FT_Init_FreeType(&freetype)) {
        freetype=NULL;
        pthread_mutex_unlock(&lock); free(library);
        fprintf(stderr,"Font: FreeType initialization failed\n");
        return ERR_FATAL;
    }
    pthread_mutex_unlock(&lock);
    library->magic=LIBRARY_MAGIC; library->memory=memory; library->edition=edition;
    *out=library;
    int major=0,minor=0,patch=0;
    FT_Library_Version(freetype,&major,&minor,&patch);
    printf("[DEBUG_SAULO][Font] library created (edition %#llx, FreeType %d.%d.%d)\n",(unsigned long long)edition,major,minor,patch);
    return FONT_OK;
}

static ABI int32_t font_destroy_library(Library **pointer) {
    once("sceFontDestroyLibrary");
    if (!pointer) return ERR_INVALID_PARAMETER;
    Library *library=*pointer;
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    pthread_mutex_lock(&lock);
    library->magic=0;
    /* Fonts still open keep using it; the last sceFontCloseFont frees it. */
    int release=!library->fonts;
    pthread_mutex_unlock(&lock);
    if (release) free(library);
    *pointer=NULL;
    return FONT_OK;
}

static ABI int32_t font_support_external_fonts(Library *library, uint32_t max_fonts, uint32_t formats) {
    once("sceFontSupportExternalFonts");
    if (!library || library->magic!=LIBRARY_MAGIC) return ERR_INVALID_LIBRARY;
    printf("[DEBUG_SAULO][Font] external fonts supported (max %u, formats %#x)\n",max_fonts,formats);
    return FONT_OK;
}

static ABI int32_t font_support_system_fonts(Library *library) {
    once("sceFontSupportSystemFonts");
    return library && library->magic==LIBRARY_MAGIC ? FONT_OK : ERR_INVALID_LIBRARY;
}

static ABI int32_t font_attach_device_cache(Library *library, void *buffer, uint32_t size) {
    (void)buffer; (void)size;
    once("sceFontAttachDeviceCacheBuffer");
    return library && library->magic==LIBRARY_MAGIC ? FONT_OK : ERR_INVALID_LIBRARY;
}

static ABI int32_t font_create_renderer(const FontMemory *memory, const void *selection, uint64_t edition, Renderer **out) {
    once("sceFontCreateRendererWithEdition");
    if (!out) return ERR_INVALID_PARAMETER;
    *out=NULL;
    if (!memory || memory->kind!=MEM_MAGIC) return ERR_INVALID_MEMORY;
    if (!selection) return ERR_NO_SUPPORT_LIBRARY;
    Renderer *renderer=calloc(1,sizeof(*renderer));
    if (!renderer) return ERR_ALLOCATION_FAILED;
    renderer->magic=RENDERER_MAGIC; renderer->memory=memory; renderer->edition=edition;
    *out=renderer;
    return FONT_OK;
}

static ABI int32_t font_destroy_renderer(Renderer **pointer) {
    once("sceFontDestroyRenderer");
    if (!pointer) return ERR_INVALID_PARAMETER;
    Renderer *renderer=*pointer;
    if (!renderer || renderer->magic!=RENDERER_MAGIC) return ERR_INVALID_RENDERER;
    renderer->magic=0;
    /* Fonts may still point at it (PS4 leaves that to the caller); keep the block alive. */
    *pointer=NULL;
    return FONT_OK;
}

/* ---- Fonts ---- */
static ABI int32_t font_open_memory(Library *library, const void *data, uint32_t size, const OpenParams *params, Font **out) {
    once("sceFontOpenFontMemory");
    if (!library || library->magic!=LIBRARY_MAGIC) { if (out) *out=NULL; return ERR_INVALID_LIBRARY; }
    if (!data || !size || !out) { if (out) *out=NULL; return ERR_INVALID_PARAMETER; }
    *out=NULL;
    Font *font=calloc(1,sizeof(*font));
    if (!font) return ERR_ALLOCATION_FAILED;
    FT_Long index=params ? (FT_Long)params->subfont_index : 0;
    pthread_mutex_lock(&lock);
    /* The data stays owned by the guest, which keeps it mapped while the font is open. */
    FT_Error error=FT_New_Memory_Face(freetype,data,size,index,&font->face);
    if (error) {
        pthread_mutex_unlock(&lock); free(font);
        fprintf(stderr,"Font: open failed (%u bytes at %p, FreeType error %d)\n",size,data,error);
        return ERR_FONT_OPEN_FAILED;
    }
    if (!font->face->charmap) FT_Select_Charmap(font->face,FT_ENCODING_UNICODE);
    float scale=font->face->units_per_EM ? font->face->units_per_EM/64.0f : 16.0f;
    font->magic=FONT_MAGIC; font->library=library;
    font->scale_w=font->scale_h=font->render_w=font->render_h=scale;
    ++library->fonts;
    printf("[DEBUG_SAULO][Font] opened %s %s (%u bytes, %ld glyphs, %u units/em)\n",
           font->face->family_name ? font->face->family_name : "?",
           font->face->style_name ? font->face->style_name : "",
           size,(long)font->face->num_glyphs,font->face->units_per_EM);
    pthread_mutex_unlock(&lock);
    *out=font;
    return FONT_OK;
}

/* The PS4 system font sets live in the firmware and are not part of the game. */
static ABI int32_t font_open_font_set(Library *library, uint32_t set, uint32_t flags, const OpenParams *params, Font **out) {
    (void)params;
    once("sceFontOpenFontSet");
    printf("[DEBUG_SAULO][Font] system font set %#x (flags %#x) unavailable\n",set,flags);
    if (out) *out=NULL;
    return library && library->magic==LIBRARY_MAGIC ? ERR_FONT_OPEN_FAILED : ERR_INVALID_LIBRARY;
}

static ABI int32_t font_close(Font *handle) {
    once("sceFontCloseFont");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    if (!font) { pthread_mutex_unlock(&lock); return ERR_INVALID_FONT_HANDLE; }
    FT_Done_Face(font->face);
    font->face=NULL; font->magic=0;
    Library *library=font->library;
    int release=0;
    if (library && --library->fonts==0 && library->magic!=LIBRARY_MAGIC) release=1;
    pthread_mutex_unlock(&lock);
    if (release) free(library);
    free(font);
    return FONT_OK;
}

static ABI int32_t font_bind_renderer(Font *handle, Renderer *renderer) {
    once("sceFontBindRenderer");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (font->renderer) result=ERR_ALREADY_BOUND_RENDERER;
    else if (!renderer || renderer->magic!=RENDERER_MAGIC) result=ERR_INVALID_RENDERER;
    else {
        /* Binding starts the render style from the font's current one. */
        font->renderer=renderer; font->render_w=font->scale_w; font->render_h=font->scale_h;
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI int32_t font_unbind_renderer(Font *handle) {
    once("sceFontUnbindRenderer");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=!font ? ERR_INVALID_FONT_HANDLE : !font->renderer ? ERR_NOT_BOUND_RENDERER : FONT_OK;
    if (result==FONT_OK) font->renderer=NULL;
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI int32_t font_set_scale_pixel(Font *handle, float w, float h) {
    once("sceFontSetScalePixel");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    if (font) { font->scale_w=w; font->scale_h=h; }
    pthread_mutex_unlock(&lock);
    return font ? FONT_OK : ERR_INVALID_FONT_HANDLE;
}

static ABI int32_t font_setup_render_scale_pixel(Font *handle, float w, float h) {
    once("sceFontSetupRenderScalePixel");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=!font ? ERR_INVALID_FONT_HANDLE : !font->renderer ? ERR_NOT_BOUND_RENDERER : FONT_OK;
    if (result==FONT_OK) { font->render_w=w; font->render_h=h; }
    pthread_mutex_unlock(&lock);
    return result;
}

/* Baseline and line height from the font bounding box (head.yMax / yMin), at the font scale.
 * decorationExtent only grows with weight effects, which the game never sets. */
static ABI int32_t font_get_horizontal_layout(Font *handle, HorizontalLayout *layout) {
    once("sceFontGetHorizontalLayout");
    pthread_mutex_lock(&lock);
    Font *font=font_of(handle);
    int32_t result=FONT_OK;
    if (!font) result=ERR_INVALID_FONT_HANDLE;
    else if (!layout) result=ERR_INVALID_PARAMETER;
    else if (set_size(font,font->scale_w,font->scale_h)) result=ERR_INVALID_PARAMETER;
    else {
        FT_Fixed y_scale=font->face->size->metrics.y_scale;
        FT_Long top=FT_MulFix(font->face->bbox.yMax,y_scale), bottom=FT_MulFix(font->face->bbox.yMin,y_scale);
        if (!FT_IS_SCALABLE(font->face)) {
            top=font->face->size->metrics.ascender; bottom=font->face->size->metrics.descender;
        }
        layout->baseline_offset=top/64.0f;
        layout->line_advance=(top-bottom)/64.0f;
        layout->decoration_extent=0.0f;
    }
    if (result!=FONT_OK && layout) memset(layout,0,sizeof(*layout));
    pthread_mutex_unlock(&lock);
    return result;
}

static ABI void font_render_surface_init(RenderSurface *surface, void *buffer, int32_t width_byte,
                                         int32_t pixel_size_byte, int32_t width, int32_t height) {
    once("sceFontRenderSurfaceInit");
    if (!surface) return;
    if (width<0) width=0;
    if (height<0) height=0;
    memset(surface,0,sizeof(*surface));
    surface->buffer=buffer; surface->width_byte=width_byte; surface->pixel_size_byte=(int8_t)pixel_size_byte;
    surface->width=width; surface->height=height;
    surface->scissor_x1=(uint32_t)width; surface->scissor_y1=(uint32_t)height;
}

static const RuntimeExport exports[]={
    {"sceFontMemoryInit",font_memory_init},
    {"sceFontMemoryTerm",font_memory_term},
    {"sceFontSelectLibraryFt",font_select_library_ft},
    {"sceFontSelectRendererFt",font_select_renderer_ft},
    {"sceFontCreateLibraryWithEdition",font_create_library},
    {"sceFontDestroyLibrary",font_destroy_library},
    {"sceFontSupportExternalFonts",font_support_external_fonts},
    {"sceFontSupportSystemFonts",font_support_system_fonts},
    {"sceFontAttachDeviceCacheBuffer",font_attach_device_cache},
    {"sceFontCreateRendererWithEdition",font_create_renderer},
    {"sceFontDestroyRenderer",font_destroy_renderer},
    {"sceFontOpenFontMemory",font_open_memory},
    {"sceFontOpenFontSet",font_open_font_set},
    {"sceFontCloseFont",font_close},
    {"sceFontBindRenderer",font_bind_renderer},
    {"sceFontUnbindRenderer",font_unbind_renderer},
    {"sceFontSetScalePixel",font_set_scale_pixel},
    {"sceFontSetupRenderScalePixel",font_setup_render_scale_pixel},
    {"sceFontGetHorizontalLayout",font_get_horizontal_layout},
    {"sceFontRenderSurfaceInit",font_render_surface_init},
};
uintptr_t runtime_font_resolve(const char *name) {
    return RUNTIME_LOOKUP(exports,name);
}
