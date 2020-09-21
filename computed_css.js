let loaded_notes = [];

function css_property_set(property_name, value)
{
    document.querySelector(":root").style.setProperty(property_name, value);
}

let screen_height = document.documentElement.clientHeight;
let header_height = document.getElementById("header").offsetHeight;

css_property_set ("--note-container-height", screen_height - header_height + "px");
