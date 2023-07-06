use adw::prelude::*;
use adw::subclass::navigation_page::NavigationPageImpl;
use gtk::glib;
use gtk::subclass::prelude::*;
use gtk::CompositeTemplate;

use anyhow::Context;
use font_kit::handle::Handle as FkHandle;

const CHARACTERS: [char; 89] = [
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
    'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '0', '‘', '?', '’', '“', '!', '”', '(', '%', ')', '[', '#', ']', '{', '@',
    '}', '/', '&', '\\', '<', '-', '+', ',', '>', ':', ';', '.', '*',
];

mod imp {
    use super::*;

    #[derive(Debug, Default, CompositeTemplate)]
    #[template(file = "font-preview.ui")]
    pub struct FontPreview {
        #[template_child]
        pub family_label: TemplateChild<gtk::Label>,
        #[template_child]
        pub demo_label: TemplateChild<gtk::Label>,
        #[template_child]
        pub loc_demo_label: TemplateChild<gtk::Label>,
        #[template_child]
        pub flowbox: TemplateChild<gtk::FlowBox>,
        #[template_child]
        pub characters_box: TemplateChild<gtk::Box>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FontPreview {
        const NAME: &'static str = "FontPreview";
        type Type = super::FontPreview;
        type ParentType = adw::NavigationPage;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.set_css_name("preview");
        }

        fn instance_init(obj: &glib::subclass::InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for FontPreview {
        fn constructed(&self) {
            self.parent_constructed();

            let lang = pango::Language::default();
            let sample_string = lang.sample_string();
            self.loc_demo_label.set_label(&sample_string);
        }
    }

    impl WidgetImpl for FontPreview {}
    impl NavigationPageImpl for FontPreview {}
}

glib::wrapper! {
    pub struct FontPreview(ObjectSubclass<imp::FontPreview>)
        @extends gtk::Widget, adw::NavigationPage;
}

impl Default for FontPreview {
    fn default() -> Self {
        glib::Object::new()
    }
}

impl FontPreview {
    pub fn load_font(&self, file: &gio::File, index: u32) -> anyhow::Result<()> {
        let imp = self.imp();

        let path = file
            .path()
            .with_context(|| format!("File {file:?} did not have a path"))?;

        let handle = FkHandle::from_path(path, index);
        let font = handle.load()?;

        imp.family_label.set_label(&font.family_name());
        self.set_title(&font.family_name());

        let attributes = pango::AttrList::new();
        attributes.change(pango::AttrString::new_family(&font.family_name()));

        if imp
            .family_label
            .label()
            .chars()
            .any(|c| font.glyph_for_char(c).is_none())
        {
            imp.family_label.set_attributes(None);
        } else {
            imp.family_label.set_attributes(Some(&attributes));
        }

        if imp
            .loc_demo_label
            .label()
            .chars()
            .any(|c| font.glyph_for_char(c).is_none())
        {
            imp.loc_demo_label.set_attributes(None);
            imp.loc_demo_label.set_visible(false);
        } else {
            imp.loc_demo_label.set_attributes(Some(&attributes));
            imp.loc_demo_label.set_visible(true);
        }

        imp.demo_label.set_attributes(Some(&attributes));

        // FIXME Use gtk::FlowBox::remove_all from v4_12.
        while let Some(child) = imp.flowbox.first_child() {
            imp.flowbox.remove(&child);
        }

        // We only need 3 bytes for the characters in CHARACTERS.
        let mut buf = [0; 3];
        let mut empty = true;
        for character in CHARACTERS.iter() {
            if font.glyph_for_char(*character).is_none() {
                continue;
            }
            empty = false;
            let label = gtk::Label::new(Some(character.encode_utf8(&mut buf)));
            label.set_attributes(Some(&attributes));
            imp.flowbox.append(&label);
        }

        imp.characters_box.set_visible(!empty);

        Ok(())
    }
}
