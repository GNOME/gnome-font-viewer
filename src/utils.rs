use std::path::PathBuf;
use std::process::Command;

use font_kit::font::Font as FkFont;
use font_kit::properties::Properties as FkProperties;
use font_kit::properties::Style as FkStyle;
use font_kit::properties::Weight as FkWeight;
use gio::prelude::*;
use once_cell::sync::Lazy;

static CACHE_PATH: Lazy<PathBuf> = Lazy::new(|| {
    glib::user_cache_dir()
        .join("gnome-font-viewer")
        .join("fonts")
});

pub(crate) fn fk_weight_to_pango_weight(weight: FkWeight) -> pango::Weight {
    // font-kit's weights don't map exactly to pango font weights.
    // Instead of using fontconfig's values, font-kit is based on CSS.
    // Thankfully pango documents how these correspond to its enum.
    //
    // See https://docs.gtk.org/Pango/enum.Weight.html#semilight
    let weight = weight.0 as u16;
    match weight {
        100 => pango::Weight::Thin,
        200 => pango::Weight::Ultralight,
        300 => pango::Weight::Light,
        350 => pango::Weight::Semilight,
        380 => pango::Weight::Book,
        400 => pango::Weight::Normal,
        500 => pango::Weight::Medium,
        600 => pango::Weight::Semibold,
        700 => pango::Weight::Bold,
        800 => pango::Weight::Ultrabold,
        900 => pango::Weight::Heavy,
        1000 => pango::Weight::Ultraheavy,
        _ => pango::Weight::Normal,
    }
}

pub(crate) fn weight_name_from_fk_weight(weight: FkWeight) -> Option<&'static str> {
    let weight = weight.0 as u16;
    log::debug!("Font weight {weight}");

    match weight {
        100 => Some("Thin"),
        200 => Some("Extralight"),
        300 => Some("Light"),
        350 => Some("Semilight"),
        380 => Some("Book"),
        400 => Some("Regular"),
        500 => Some("Medium"),
        600 => Some("Semibold"),
        700 => Some("Bold"),
        800 => Some("Extrabold"),
        900 => Some("Heavy"),
        1000 => Some("Extrablack"),
        _ => None,
    }
}

pub(crate) fn slant_name_from_fk_style(style: FkStyle) -> Option<&'static str> {
    match style {
        FkStyle::Italic => Some("Italic"),
        FkStyle::Oblique => Some("Oblique"),
        _ => None,
    }
}

pub(crate) fn font_name_from_fk_font(font: &FkFont, props: &FkProperties) -> String {
    let fk_weight = props.weight;
    let style = props.style;
    let weight_name = weight_name_from_fk_weight(fk_weight);
    let slant_name = slant_name_from_fk_style(style);
    let family_name = font.family_name();
    let full_name = font.full_name();

    // full_name == family_name means that font kit probably didn't find a full name.
    if full_name == family_name || full_name.is_empty() {
        let style_name = match (weight_name, slant_name) {
            (Some(weight_name), Some(slant_name)) => format!("{weight_name} {slant_name}"),
            (Some(weight_name), None) => format!("{weight_name}"),
            (None, Some(slant_name)) => format!("{slant_name}"),
            (None, None) => String::new(),
        };

        if family_name.is_empty() || style_name.is_empty() || style_name == "Regular" {
            family_name
        } else {
            format!("{family_name}, {style_name}")
        }
    } else {
        full_name
    }
}

fn refresh_cache() -> anyhow::Result<()> {
    Command::new("fc-cache")
        .arg("-f")
        .arg("-v")
        .arg(&*CACHE_PATH)
        .output()?;

    Ok(())
}

pub fn cache_font(font: &gio::File) -> anyhow::Result<gio::File> {
    std::fs::create_dir_all(&*CACHE_PATH)?;

    let folder = gio::File::for_path(&*CACHE_PATH);
    let dest = folder.child(&font.basename().unwrap());
    font.copy(
        &dest,
        gio::FileCopyFlags::OVERWRITE,
        gio::Cancellable::NONE,
        None,
    )?;

    refresh_cache()?;

    Ok(dest)
}
