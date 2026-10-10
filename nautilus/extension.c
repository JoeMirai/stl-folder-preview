#include <nautilus-extension.h>
#include <gio/gio.h>

typedef struct { GObject parent; } StlPreview;
typedef struct { GObjectClass parent; } StlPreviewClass;
static void provider_init(NautilusMenuProviderInterface *iface);
G_DEFINE_DYNAMIC_TYPE_EXTENDED(StlPreview, stl_preview, G_TYPE_OBJECT, 0,
    G_IMPLEMENT_INTERFACE_DYNAMIC(NAUTILUS_TYPE_MENU_PROVIDER, provider_init))
static void launch(NautilusMenuItem *item, gpointer user_data) {
    (void)item;
    char *program=g_build_filename(g_get_home_dir(),".local","bin","stl-folder-preview",NULL);
    char *argv[]={program,(char*)user_data,NULL};
    GError *error=NULL;
    if(!g_spawn_async(NULL,argv,NULL,G_SPAWN_DEFAULT,NULL,NULL,NULL,&error)) {
        g_warning("STL preview: %s",error->message);g_error_free(error);
    }
    g_free(program);
}
static GList *item_for(NautilusFileInfo *file) {
    if(!nautilus_file_info_is_directory(file)) return NULL;
    char *uri=nautilus_file_info_get_uri(file);char *path=g_filename_from_uri(uri,NULL,NULL);g_free(uri);
    if(!path) return NULL;
    NautilusMenuItem *item=nautilus_menu_item_new("StlPreview::render","Render STL Preview","Preview settings and render STL thumbnails for this folder","image-x-generic");
    g_object_set_data_full(G_OBJECT(item),"folder",path,g_free);
    g_signal_connect(item,"activate",G_CALLBACK(launch),path);
    return g_list_append(NULL,item);
}
static GList *file_items(NautilusMenuProvider *provider,GList *files) {
    (void)provider;if(!files || files->next)return NULL;return item_for(files->data);
}
static GList *background_items(NautilusMenuProvider *provider,NautilusFileInfo *folder) {
    (void)provider;return item_for(folder);
}
static void provider_init(NautilusMenuProviderInterface *iface) {iface->get_file_items=file_items;iface->get_background_items=background_items;}
static void stl_preview_init(StlPreview *self) {(void)self;}
static void stl_preview_class_init(StlPreviewClass *klass) {(void)klass;}
static void stl_preview_class_finalize(StlPreviewClass *klass) {(void)klass;}
void nautilus_module_initialize(GTypeModule *module) {stl_preview_register_type(module);}
void nautilus_module_shutdown(void) {}
void nautilus_module_list_types(const GType **types,int *count) {static GType list[1];list[0]=stl_preview_get_type();*types=list;*count=1;}
