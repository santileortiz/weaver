/*
 * Copyright (C) 2019 Santiago León O.
 */

// The cli_opt parser I had before (lets call it parser V1) started to become
// too complicated for what it actually did. This is an attempt to go back to
// the simplest thing possible.
//
// For the parser V1 I tried to 'prepare' for future 2 features that I never got
// to implement and made the API complicated. The features were:
//   1) Descriptive error messages about invalid syntax of CLI options
//   2) automaticaly generated documentation for the application
//
// These are not bad ideas, but trying to do these from the code force us to
// create an iternal representation of CLI options. Then supported CLI options
// are limited by the data model chosen for this IR. Making things more flexible
// implies adding iterators or callbacks. This is not the kind of easy to use
// API I had in mind.
//
// The new idea is to have a set of functions that find options in the argv
// array. For now, error handling will be left to the user. Documentation wise,
// I have yet to know how to create man pages, when I do learn about this I
// think the correct approach is to run a small 'parser' of C code that has
// calls to CLI option getters, and generates the documentation, or code that
// eases documentation creation.
//
//                                              - Santiago (August 12, 2019)
//
// This function looks for opt and expects the next argv to exist and be the
// argument for opt. A pointer to this arg will be returned. Some ideas come to
// mind on how to make this more sophisticated:
//
//  * Let opt be a comma separated string like "--list,-l" to allow long and
//    short forms of options.
//
//  * Let the argument be a comma separated list of strings, something like:
//      --exclude=*.c,*h
//    What would be the return type in this case? Do we allow space after the =
//    sign? Maybe it should be a different function.
//
//  * Add a values argument so the caller can specify what they expect as
//    result, this way we can show better error messages.
//
//  * Add a default argument to return something even if the option is not
//    found.
//
// I won't implement any of those until I actually need them. That's what made
// the cli parser V1 be over engineered.
char* get_cli_arg_opt (char *opt, char **argv, int argc)
{
    char *arg = NULL;

    for (int i=1; arg==NULL && i<argc; i++) {
        if (strcmp (opt, argv[i]) == 0) {
            if (i+1 < argc) {
                arg = argv[i+1];
            } else {
                printf ("Expected argument for option %s.", opt);
            }
        }
    }

    return arg;
}

// This function looks for opt in the argv array and returns true if it finds it
// in it, and false otherwise.
bool get_cli_bool_opt (char *opt, char **argv, int argc)
{
    bool found = false;

    for (int i=1; found==false && i<argc; i++) {
        if (strcmp (opt, argv[i]) == 0) {
            found = true;
        }
    }

    return found;
}

// Returns the first element of argv not preceded by a string starting with '-'.
// TODO: This isn't generic, We need an array of all boolean CLI options so that
// we can skip their arguments.
//
// Maybe this function should be autogenerated based on a pre processing of the
// source code that uses the cli option API.
char* get_cli_no_opt_arg (char **argv, int argc)
{
    static char *bool_opts[] = {"--write-output", "--unsafe"};
    char *arg = NULL;

    for (int i=1; arg==NULL && i<argc; i++) {
        if (argv[i][0] == '-') {
            bool found = false;
            for (int j=0; !found && j<ARRAY_SIZE(bool_opts); j++) {
                if (strcmp (bool_opts[j], argv[i]) == 0) {
                    found = true;
                }
            }

            // This option receives an argument skip it.
            if (!found) {
                i++;
            }

        } else {
            arg = argv[i];
        }
    }

    return arg;
}
