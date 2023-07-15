use crate::config;
use crate::window::FontViewWindow;

use gio::prelude::*;
use gtk::prelude::*;

use once_cell::sync::OnceCell;

mod imp {
    use super::*;
    use adw::subclass::prelude::*;
    use glib::WeakRef;

    #[derive(Debug, Default)]
    pub struct FontViewApplication {
        pub(super) window: OnceCell<WeakRef<FontViewWindow>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FontViewApplication {
        const NAME: &'static str = "FontViewApplication";
        type Type = super::FontViewApplication;
        type ParentType = adw::Application;
    }

    impl ObjectImpl for FontViewApplication {}

    impl ApplicationImpl for FontViewApplication {
        fn startup(&self) {
            let app = self.obj();

            self.parent_startup();

            let actions = [
                gio::ActionEntry::builder("about")
                    .activate(|app: &Self::Type, _, _| app.action_about())
                    .build(),
                gio::ActionEntry::builder("quit")
                    .activate(|app: &Self::Type, _, _| app.action_quit())
                    .build(),
            ];

            app.add_action_entries(actions);
        }

        fn activate(&self) {
            let window = self.ensure_window();
            window.show_overview();
        }

        fn open(&self, files: &[gio::File], _hint: &str) {
            // At least one file is guaranteed if this vfunc is called
            let file = files.get(0).unwrap();

            match crate::utils::cache_font(&file) {
                Err(err) => log::error!("Could not cache font: {err}"),
                Ok(file) => {
                    let window = self.ensure_window();

                    if let Err(err) = window.show_preview(&file, 0) {
                        log::error!("Could not show preview: {err}");
                        // TODO Should we close the window here?
                    }
                }
            }
        }
    }

    impl GtkApplicationImpl for FontViewApplication {}
    impl AdwApplicationImpl for FontViewApplication {}

    impl FontViewApplication {
        fn ensure_window(&self) -> FontViewWindow {
            let app = self.obj();

            let window = if let Some(window) = self.window.get().and_then(|w| w.upgrade()) {
                window
            } else {
                let window = FontViewWindow::new(&app);
                self.window.set(glib::ObjectExt::downgrade(&window)).unwrap();
                window
            };

            window.present();

            window
        }
    }
}

glib::wrapper! {
    pub struct FontViewApplication(ObjectSubclass<imp::FontViewApplication>)
        @extends adw::Application, gtk::Application, gio::Application,
        @implements gio::ActionGroup, gio::ActionMap;
}

impl FontViewApplication {
    pub fn new() -> Self {
        glib::Object::builder()
            .property("application-id", config::APP_ID)
            .property("flags", gio::ApplicationFlags::HANDLES_OPEN)
            .property("resource-base-path", "/org/gnome/font-viewer/")
            .build()
    }

    fn action_about(&self) {
        let about = adw::AboutWindow::builder()
            .version(config::VERSION)
            .developers([
                "Christopher Davis",
                "Evan Welsh",
                "Cosmio Cecchi",
                "James Henstridge",
            ])
            .application_name("Fonts") // TODO: i18n
            .application_icon(config::APP_ID)
            .developer_name("The GNOME Project")
            .website("https://gitlab.gnome.org/GNOME/gnome-font-viewer")
            .issue_url("https://gitlab.gnome.org/GNOME/gnome-font-viewer/-/issues")
            // TODO: .translator_credits(translator_credits)
            .license_type(gtk::License::Gpl20)
            .build();

        about.set_transient_for(self.active_window().as_ref());

        about.present();
    }

    fn action_quit(&self) {
        while let Some(window) = self.active_window() {
            window.close();
        }
    }
}
