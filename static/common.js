function css_property_set(property_name, value)
{
    document.querySelector(":root").style.setProperty(property_name, value);
}

function ajax_get (target, callback)
{
    if (typeof(target) === "string") {
        let request = new XMLHttpRequest();
        request.onreadystatechange = function() { 
            if (this.readyState == 4) {
                callback(request.responseText);
            }
        }
        request.open("GET", target, true);
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

