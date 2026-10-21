qt_add_executable(tst_document tst_document.cpp)
target_link_libraries(tst_document PRIVATE guckste_core Qt6::Test)
add_test(NAME tst_document COMMAND tst_document)
set_tests_properties(tst_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_ui tst_ui.cpp)
target_link_libraries(tst_ui PRIVATE guckste_qml guckste_qmlplugin Qt6::Test Qt6::QuickTest Qt6::Widgets)
add_test(NAME tst_ui COMMAND tst_ui)
set_tests_properties(tst_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_recorder tst_recorder.cpp)
target_link_libraries(tst_recorder PRIVATE guckste_core Qt6::Test)
add_test(NAME tst_recorder COMMAND tst_recorder)
set_tests_properties(tst_recorder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
