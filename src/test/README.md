### Compiling/running unit tests

Unit tests will be automatically compiled if dependencies were met in `./configure`
and tests weren't explicitly disabled.

After configuring, they can be run with `make check`.

To run the ravend tests manually, launch `src/test/test_raven`. To recompile
after a test file was modified, run `make` and then run the test again. If you
modify a non-test file, use `make -C src/test` to recompile only what's needed
to run the ravend tests.

To add more ravend tests, add `BOOST_AUTO_TEST_CASE` functions to the existing
.cpp files in the `test/` directory or add new .cpp files that
implement new BOOST_AUTO_TEST_SUITE sections.

To run the raven-qt tests manually, launch `src/qt/test/test_raven-qt`

To add more raven-qt tests, add them to the `src/qt/test/` directory and
the `src/qt/test/test_main.cpp` file.

To display progress information the unit tests should be run as follows:

`test_runner --show_progress=true --colour_output=true`

Additional optional parameters are available. To display all optional parameters run:

`test_runner --help`

### Debugging unit tests

To display what individual tests are running (as they are running) use the
`--log_level=message` parameter.  

By default the log messages from the Raven Core application are not echoed 
when running the unit tests.  If it is desired to print this log data change 
the following from 'false' to 'true' in the `test_raven.cpp` file and uncomment
three lines in the `script\interpreter.cpp\ VerifyScript` method and recompile:

    src\test\test_raven.cpp:
    fPrintToConsole = false;  <-to->  fPrintToConsole = true;

    script\interpreter.cpp\ VerifyScript method, uncomment:
    //std::string str;
    //str.assign(ScriptErrorString(*serror));
    //std::cout << str << std::endl;

Previously several individual tests had the 'fPrintToConsole' parameter defaulted to 
'true' causingthe unit test log window to be filled with superfluous log-data making 
it appear that the tests were failing.

### Running individual tests

Run `test_raven --list_content` to get a full list of available unit tests.

To run just the 'getarg_tests' (verbosely):

    test_raven --run_test=getarg_tests

... or to run just the doubledash test:

    test_raven --run_test=getarg_tests/doubledash_test

### Note on adding test cases

The sources in this directory are unit test cases.  Boost includes a
unit testing framework, and since raven already uses boost, it makes
sense to simply use this framework rather than require developers to
configure some other framework (we want as few impediments to creating
unit tests as possible).

The build system is setup to compile an executable called `test_raven`
that runs all of the unit tests.  The main source file is called
test_raven.cpp. To add a new unit test file to our test suite you need 
to add the file to `src/Makefile.test.include`. The pattern is to create 
one test file for each class or source file for which you want to create 
unit tests.  The file naming convention is `<source_filename>_tests.cpp` 
and such files should wrap their tests in a test suite 
called `<source_filename>_tests`. For an example of this pattern, 
examine `uint256_tests.cpp`.

For further reading, I found the following website to be helpful in
explaining how the boost unit test framework works:
[http://www.alittlemadness.com/2009/03/31/c-unit-testing-with-boosttest/](http://www.alittlemadness.com/2009/03/31/c-unit-testing-with-boosttest/).
