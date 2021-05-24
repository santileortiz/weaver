function css_property_set(property_name, value)
{
    document.querySelector(":root").style.setProperty(property_name, value);
}

function ajax_get (target, callback, async=true)
{
    if (typeof(target) === "string") {
        let request = new XMLHttpRequest();
        request.onreadystatechange = function() { 
            if (this.readyState == 4) {
                callback(request.responseText);
            }
        }
        request.open("GET", target, async);
        request.send(null);

    } else { // assume target is an array of URLs
        let count = 0;
        let responses = [];
        for (let i=0; i<target.length; i++) {
            let request = new XMLHttpRequest();
            request.onreadystatechange = function() { 
                if (this.readyState == 4 /*DONE*/) {
                    responses[i] = request.responseText;
                    count++;

                    if (count === target.length) {
                        callback (responses);
                    }
                }
            }
            request.open("GET", target[i], true);
            request.send(null);
        }
    }
}

// Compute a dictionary of URL paramer keys to an array of values mapped to
// that key.
//
// These are relevant implementation details:
//  - Return null if there are no query parameters.
//  - Optionally pass a "flags" array where parameters that don't have a value
//    (like "...&flag&...") will be pushed once.
//  - Keys and values are URL decoded.
//  - Query parameters like "...&key=&..." will get the empty string as value.
//  - For each key, its array of values is guaranteed to be in the order they
//    were in the URL.
//  - Percent encoding of UTF-8 octets will be correctly decoded (%C3%B1 is
//    decoded to é).
//  - If a flags array is passed, flags not already present will be added in
//    the order they were in the URL. Flags already present are left in the
//    original position.
//  - Don't replace + with space.
//
// TODO: Test all these details are true in most browsers. Tested in Chrome.
//
// Why + isn't replaced to space
// -----------------------------
//
// The use of + characters as space is specific to the context where URL
// parameters are being parsed. The URI specification [1] never mentions that
// the + should be translated to the space character. It is the HTML
// specification that defines + as space in the form data serialization
// (it uses the application/x-www-form-urlencoded [2] serialization format).
// When using forms with the GET method [3], form data is sent appended to the
// URI [4], as query parameters (this is different from a form that uses POST,
// where form data is sent as the body of the HTTP request [5]).
//
// This function was written for the use case of client side parsing of URL
// parameters. In this case, we can't know if the query component of the URI
// has been serialized with the application/x-www-form-urlencoded format, so we
// don't replace + to space.
//
// [1]: https://tools.ietf.org/html/rfc3986
// [2]: https://url.spec.whatwg.org/#application/x-www-form-urlencoded
// [3]: https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm
// [4]: https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#submit-mutate-action
// [5]: https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#submit-body
function url_parameters(flags)
{
    let params = null

    // NOTE: At least in Chrome, when using an IRI, its UTF-8 characters get
    // percent encoded to transform it into a URL. That's what we get from
    // window.location.href. Not sure if this happens in all browsers.
    let query = /\?([^#]+)/.exec(window.location.href)
    if (query != null) {
        params = {}

        let parameter_components = query[1].split("&")
        for (let i=0; i<parameter_components.length; i++) {
            // This regex allows distinguishing between a query component being:
            // 1. "key" -> "key" is a flag
            // 2. "key=" -> "key" parameter is set to "" (empty string)
            // 3. "key=value" -> "key" parameter is set to "value"
            let key_value = /([^=]+)(?:=(.*))?/.exec(parameter_components[i])
            if (key_value[2] != undefined) {
                let key = decodeURIComponent(key_value[1])
                if (params[key] == undefined) {
                    params[key] = []
                }
                params[key].push(decodeURIComponent(key_value[2]))

            } else if (flags != undefined && flags != null) {
                let flag = decodeURIComponent(key_value[1])
                if (!flags.includes(flag)) {
                    flags.push(flag)
                }
            }
        }
    }

    return params
}

// Color Utilities

// This function parses the CSS syntax for colors and returns an array of 4
// numbers. First 3 values are red, green and blue (0 - 255), the last one ir
// the alpha channel (0 - 1).
//
// color_css_parse ("rgb(255, 255, 255)") // -> [255,255,255]
// color_css_parse ("rgba( 255, 255 , 255 , 1)") // -> [255,255,255,1]
// color_css_parse ("rgba(255,255,255,100%)") // -> [255,255,255,1]
// color_css_parse ("#FF00FF00") // -> [255,0,255,0]
// color_css_parse ("#00FF00") // -> [0,255,0,1]
function color_css_parse (str)
{
    let ColorType = {
        UNKNOWN: 0,
        RGB: 1,
        RGBA: 2,
        HEX: 3
    }

    str = str.trim()

    let pos = 0
    let color_type = ColorType.UNKNOWN
    {
        is_keyword = false
        let keywords = ["rgb(", "rgba(", "#"]
        let type = [ColorType.RGB, ColorType.RGBA, ColorType.HEX]
        let i = 0
        for (; i<keywords.length; i++) {
            if (str.startsWith(keywords[i], pos)) {
                is_keyword = true
                break;
            }
        }

        pos += keywords[i].length
        if (is_keyword) {
            color_type = type[i]
        }
    }

    let color = []
    if (color_type === ColorType.RGB || color_type === ColorType.RGBA) {
        let end = str.indexOf(")")
        if (end != -1) {
            let content = str.substr(pos, end - pos)
            let arr = content.split (",")

            if (arr.length >= 3) {
                for (let i=0; i<3; i++) {
                    color.push (Number(arr[i]))
                }

                if (arr.length == 4) {
                    let alpha_str = arr[3]
                    if (color_type === ColorType.RGBA) {
                        let percent_idx = alpha_str.trim().search("%")
                        if (percent_idx != -1) {
                            color.push (Number(alpha_str.substr(0, percent_idx))/100)
                        } else {
                            color.push (Number(alpha_str))
                        }

                    } else {
                        color = null
                    }

                } else if (color_type !== ColorType.RGB) {
                    color = null
                }
            }
        }

    } else if (color_type === ColorType.HEX && (str.length === 7 || str.length === 9)) {
        let hex = str.substr(1)

        for (let i=0; i<3; i++) {
            color.push(parseInt(hex.substr(i*2, 2), 16))
        }

        if (str.length === 9) {
            color.push(parseInt(hex.substr(6, 2), 16)/255)
        } else {
            color.push(1)
        }

    } else {
        color = null
    }

    // TODO: Maybe the internal representation should premultiply the alpha so
    // that the alpha blending computation is simpler.
    // :premultiply_alpha

    //console.log("color_css_parse (\"" + str + "\") // -> [" + color + "]")
    return color
}

function color_rgba (color)
{
    return "rgba(" + color.join(",") + ")"
}

// Color Operations
// ----------------
//
// Functions prefixed by _ use an internal array representation of color.
// Functions without the prefix parse the input from a string and serialize the
// output back to a string.

// Alpha blending
function _alpha_blend (bg, fg)
{
    let out_a = fg[3] + bg[3]*(1 - fg[3])

    // :premultiply_alpha
    let res = [0, 0, 0, out_a]
    if (out_a > 0) {
        for (let i=0; i<3; i++) {
            res[i] = (fg[i]*fg[3] + bg[i]*bg[3]*(1 - fg[3])) / out_a
        }
    }

    return res
}

function alpha_blend (bg_str, fg_str)
{
    let bg = color_css_parse (bg_str)
    let fg = color_css_parse (fg_str)
    return color_rgba (_alpha_blend(bg, fg))
}
