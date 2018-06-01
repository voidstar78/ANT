#ifndef ANT_COMMON_PROGRAM_OPTIONS_HANDLER_H
#define ANT_COMMON_PROGRAM_OPTIONS_HANDLER_H

namespace ANT {

// Be aware that this function has an internal try-catch on std::exception.  TODO: Is there a way to make this more explicit?
// In any case, the try-catch is specific to this build/version and is an implementation decision that could change in the future.
// This function could return a boolean to indicate "success" in loading the program option.  However it was decided that
// "success" is ambiguous since all the program options have reasonable defaults.  In the case of any single errant program 
// option specification, we'd like to attempt to proceed with the defaults.
void initialize_program_options(const int& argc, char** const argv);

}

#endif
