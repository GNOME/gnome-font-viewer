use gettextrs::*;
use gio::{self, prelude::*};

mod application;
mod config;
mod font_preview;
mod model;
mod utils;
mod window;

use application::FontViewApplication;

fn main() -> glib::ExitCode {
    env_logger::Builder::from_default_env()
        .format_timestamp_millis()
        .init();

    setlocale(LocaleCategory::LcAll, "");
    bindtextdomain("gnome-font-viewer", config::LOCALEDIR).unwrap();
    textdomain("gnome-font-viewer").unwrap();

    let res = gio::Resource::load(config::PKGDATADIR.to_owned() + "/gnome-font-viewer.gresource")
        .expect("Could not initialize resources.");
    gio::resources_register(&res);

    glib::set_application_name("Fonts");

    let app = FontViewApplication::new();
    app.run()
}
