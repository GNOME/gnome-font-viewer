use crate::application::FontViewApplication;
use crate::config;
use crate::font_preview::FontPreview;
use crate::model::{FontViewModel, FontViewModelItem};

use gio::prelude::*;
use gio::subclass::prelude::*;
use gtk::prelude::*;

use once_cell::sync::OnceCell;

mod imp {
    use std::cell::RefCell;

    use super::*;
    use adw::subclass::prelude::*;
    use glib::subclass::InitializingObject;
    use gtk::CompositeTemplate;

    #[derive(Debug, Default, CompositeTemplate)]
    #[template(file = "font-view-window.ui")]
    pub struct FontViewWindow {
        #[template_child]
        pub(super) nav_view: TemplateChild<adw::NavigationView>,

        // Overview
        #[template_child]
        pub(super) search_button: TemplateChild<gtk::ToggleButton>,
        #[template_child]
        pub(super) search_bar: TemplateChild<gtk::SearchBar>,
        #[template_child]
        pub(super) search_entry: TemplateChild<gtk::SearchEntry>,
        #[template_child]
        pub(super) grid_view: TemplateChild<gtk::GridView>,

        #[template_child]
        pub(super) font_preview: TemplateChild<FontPreview>,

        #[template_child]
        pub(super) sort_model: TemplateChild<gtk::SortListModel>,

        pub(super) model: OnceCell<FontViewModel>,
        pub(super) font_widget: OnceCell<gtk::Widget>,
        pub(super) font_file: RefCell<Option<gio::File>>,
        pub(super) cancellable: RefCell<Option<gio::Cancellable>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FontViewWindow {
        const NAME: &'static str = "FontViewWindow";
        type Type = super::FontViewWindow;
        type ParentType = adw::ApplicationWindow;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            Self::Type::bind_template_callbacks(klass);

            klass.install_action("win.back", None, move |win, _, _| {
                win.show_overview();
            });

            klass.install_action("win.toggle-search", None, move |win, _, _| {
                let imp = win.imp();
                imp.search_button.set_active(!imp.search_button.is_active());
            });

            klass.install_action("win.install-font", None, move |win, _, _| unimplemented!());
        }

        fn instance_init(obj: &InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for FontViewWindow {
        fn constructed(&self) {
            self.parent_constructed();

            let win = self.obj();

            let model = FontViewModel::new();
            let list_model = model.list_model();
            // list_model.connect_items_changed(clone!(@weak win => move|_, _, _, _| {
            //     win.refresh_install_button();
            // }));

            self.sort_model.set_model(Some(list_model));

            self.model.set(model).unwrap();

            self.search_bar.connect_entry(&self.search_entry.get());

            if config::APP_ID.ends_with("Devel") {
                win.add_css_class("devel");
            }
        }
    }

    impl WidgetImpl for FontViewWindow {}
    impl WindowImpl for FontViewWindow {}
    impl ApplicationWindowImpl for FontViewWindow {}
    impl AdwApplicationWindowImpl for FontViewWindow {}
}

glib::wrapper! {
    pub struct FontViewWindow(ObjectSubclass<imp::FontViewWindow>)
        @extends adw::ApplicationWindow, gtk::ApplicationWindow, gtk::Window, gtk::Widget,
        @implements gio::ActionGroup, gio::ActionMap, gtk::Accessible, gtk::Buildable,
                    gtk::ConstraintTarget, gtk::Native, gtk::Root, gtk::ShortcutManager;
}

#[gtk::template_callbacks]
impl FontViewWindow {
    pub fn new(app: &FontViewApplication) -> Self {
        glib::Object::builder().property("application", app).build()
    }

    pub fn show_overview(&self) {
        let imp = self.imp();

        imp.font_file.replace(None);

        imp.nav_view.pop();
    }

    pub fn show_preview(&self, file: &gio::File, face_index: u32) -> anyhow::Result<()> {
        let imp = self.imp();

        imp.font_preview.load_font(file, face_index)?;
        imp.nav_view.push(&*imp.font_preview);

        Ok(())
    }

    pub fn show_error(&self) {
        todo!("Implement going back to the overview and showing an error dialog");
    }

    #[template_callback]
    pub fn font_name_closure(item: FontViewModelItem) -> String {
        item.font_name()
    }

    #[template_callback]
    pub fn font_attribute_closure(list_item: gtk::ListItem) -> pango::AttrList {
        let list = pango::AttrList::new();

        if let Some(item) = list_item.item().and_downcast::<FontViewModelItem>() {
            let attr = pango::AttrFontDesc::new(&item.font_description());
            list.insert(attr);
        }

        list.copy().unwrap()
    }

    #[template_callback]
    pub fn preview_visible_child_closure(&self, info_active: bool) -> String {
        let child_name = if info_active { "info" } else { "preview" };
        child_name.to_string()
    }

    #[template_callback]
    pub fn view_child_activated_cb(&self, pos: u32, view: &gtk::GridView) {
        let Some(item) = view
            .model()
            .and_then(|m| m.item(pos))
            .and_downcast::<FontViewModelItem>() else {
                return;
        };

        let font_file = item.file();
        let face_index = item.face_index();

        if let Err(err) = self.show_preview(&font_file, face_index) {
            log::error!("Could not show preview: {err}");
        }
    }
}
