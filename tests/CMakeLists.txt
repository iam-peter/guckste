# Each test writes its report to a file too, which the nightly keeps when
# a test fails on a platform not at hand.
set(test_reports ${CMAKE_BINARY_DIR}/test-reports)
file(MAKE_DIRECTORY ${test_reports})

qt_add_executable(tst_document tst_document.cpp)
target_link_libraries(tst_document PRIVATE guckste_core Qt6::Test)
add_test(NAME tst_document COMMAND tst_document -o ${test_reports}/tst_document.txt,txt -o -,txt)
set_tests_properties(tst_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_ui tst_ui.cpp)
target_link_libraries(tst_ui PRIVATE guckste_qml guckste_qmlplugin Qt6::Test Qt6::QuickTest Qt6::Widgets)
add_test(NAME tst_ui COMMAND tst_ui -o ${test_reports}/tst_ui.txt,txt -o -,txt)
set_tests_properties(tst_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_recorder tst_recorder.cpp)
target_link_libraries(tst_recorder PRIVATE guckste_core Qt6::Test)
add_test(NAME tst_recorder COMMAND tst_recorder -o ${test_reports}/tst_recorder.txt,txt -o -,txt)
set_tests_properties(tst_recorder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
