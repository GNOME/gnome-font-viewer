use crate::utils::*;

use gio::prelude::*;
use gio::subclass::prelude::*;

use anyhow::Result;
use font_kit::{font::Font as FkFont, handle::Handle as FkHandle};

mod imp {
    use super::*;

    use glib::Properties;
    use once_cell::sync::OnceCell;

    #[derive(Debug, Default, Properties)]
    #[properties(wrapper_type = super::FontViewModelItem)]
    pub struct FontViewModelItem {
        #[property(get)]
        pub(super) face_index: OnceCell<u32>,
        #[property(get)]
        pub(super) file: OnceCell<gio::File>,
        #[property(get)]
        pub(super) font_name: OnceCell<String>,
        #[property(get)]
        pub(super) font_description: OnceCell<pango::FontDescription>,

        pub(super) font: OnceCell<FkFont>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FontViewModelItem {
        const NAME: &'static str = "FontViewModelItem";
        type Type = super::FontViewModelItem;
    }

    impl ObjectImpl for FontViewModelItem {
        fn properties() -> &'static [glib::ParamSpec] {
            Self::derived_properties()
        }

        fn set_property(&self, id: usize, value: &glib::Value, pspec: &glib::ParamSpec) {
            self.derived_set_property(id, value, pspec)
        }

        fn property(&self, id: usize, pspec: &glib::ParamSpec) -> glib::Value {
            self.derived_property(id, pspec)
        }
    }

    #[derive(Debug, Default)]
    pub struct FontViewModel {
        pub(super) inner: OnceCell<gio::ListStore>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FontViewModel {
        const NAME: &'static str = "FontViewModel";
        type Type = super::FontViewModel;
    }

    impl ObjectImpl for FontViewModel {}
}

glib::wrapper! {
    pub struct FontViewModelItem(ObjectSubclass<imp::FontViewModelItem>);
}

impl FontViewModelItem {
    pub(crate) fn from_handle(handle: FkHandle) -> Result<Self> {
        let item = glib::Object::builder::<Self>().build();
        let imp = item.imp();

        if let FkHandle::Path { path, font_index } = &handle {
            imp.file.set(gio::File::for_path(path)).unwrap();
            imp.face_index.set(*font_index).unwrap();
        } else {
            panic!("Font handles should be Path handles.");
        }

        let font = handle.load()?;

        let props = font.properties();
        let fk_weight = props.weight;
        let weight = fk_weight_to_pango_weight(fk_weight);

        let font_name = font_name_from_fk_font(&font, &props);
        imp.font_name.set(font_name).unwrap();

        let mut font_description = pango::FontDescription::new();
        font_description.set_family(&font.family_name());
        font_description.set_weight(weight);
        font_description.set_style(pango::Style::Normal);

        imp.font_description.set(font_description).unwrap();
        imp.font.set(font).unwrap();

        Ok(item)
    }
}

glib::wrapper! {
    pub struct FontViewModel(ObjectSubclass<imp::FontViewModel>);
}

impl FontViewModel {
    pub(crate) fn new() -> Self {
        let model = glib::Object::builder::<Self>().build();
        let imp = model.imp();

        let inner = gio::ListStore::new(FontViewModelItem::static_type());

        // TODO: Load on another thread, and find a way to filter out items
        // like the C version.
        let source = font_kit::source::SystemSource::new();

        if let Ok(handles) = source.all_fonts() {
            for handle in handles {
                if let Ok(item) = FontViewModelItem::from_handle(handle) {
                    inner.append(&item);
                }
            }
        };

        imp.inner.set(inner).unwrap();

        model
    }

    pub(crate) fn list_model(&self) -> &gio::ListStore {
        self.imp().inner.get().unwrap()
    }
}
