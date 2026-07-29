# Java binding
find_package(Java COMPONENTS Development)
find_package(JNI)

if(Java_FOUND AND JNI_FOUND)
    message(STATUS "Java ${Java_VERSION} and JNI found")
    
    # JNI library sources
    set(JNI_SOURCES
        jniutil.c
        pokutil.c
    )
    
    # Create JNI library
    add_library(poker-eval-jni SHARED ${JNI_SOURCES})
    
    # Link with poker-eval library
    if(TARGET poker_eval_modular)
        target_link_libraries(poker-eval-jni PRIVATE poker_eval_modular)
    elseif(TARGET poker-eval)
        target_link_libraries(poker-eval-jni PRIVATE poker-eval)
    else()
        target_link_libraries(poker-eval-jni PRIVATE poker-eval-static)
    endif()
    
    # Include directories
    target_include_directories(poker-eval-jni PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/inlines
        ${JNI_INCLUDE_DIRS}
    )
    
    # Set properties
    set_target_properties(poker-eval-jni PROPERTIES
        OUTPUT_NAME "pokereval-jni"
        PREFIX ""
    )
    
    # Platform-specific settings
    if(WIN32)
        set_target_properties(poker-eval-jni PROPERTIES SUFFIX ".dll")
    elseif(APPLE)
        set_target_properties(poker-eval-jni PROPERTIES SUFFIX ".jnilib")
    endif()
    
    # Java source files
    file(GLOB_RECURSE JAVA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/org/*.java")
    
    # Create JAR file
    set(JAR_NAME "poker-eval-${PROJECT_VERSION}.jar")
    set(JAR_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${JAR_NAME}")
    
    # Compile Java sources
    add_custom_command(
        OUTPUT ${JAR_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/classes
        COMMAND ${Java_JAVAC_EXECUTABLE} -d ${CMAKE_CURRENT_BINARY_DIR}/classes ${JAVA_SOURCES}
        COMMAND ${Java_JAR_EXECUTABLE} cf ${JAR_OUTPUT} -C ${CMAKE_CURRENT_BINARY_DIR}/classes .
        DEPENDS ${JAVA_SOURCES}
        COMMENT "Building Java JAR file"
    )
    
    add_custom_target(poker-eval-jar ALL DEPENDS ${JAR_OUTPUT})
    
    # Generate JNI headers
    add_custom_target(generate-jni-headers
        COMMAND ${Java_JAVAC_EXECUTABLE} -h ${CMAKE_CURRENT_BINARY_DIR}/include ${JAVA_SOURCES}
        DEPENDS ${JAVA_SOURCES}
        COMMENT "Generating JNI headers"
    )
    
    # Install targets
    install(TARGETS poker-eval-jni
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    
    install(FILES ${JAR_OUTPUT}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/java
    )
    
    # Install dependency JARs
    install(FILES
        gnu-getopt-1.0.9.jar
        jakarta-oro-2.0.6.jar
        junit-3.7.jar
        DESTINATION ${CMAKE_INSTALL_DATADIR}/java
        OPTIONAL
    )
    
    # Add test for Java binding
    if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test.java")
        add_test(NAME test_java_binding
            COMMAND ${Java_JAVA_EXECUTABLE} -cp ${JAR_OUTPUT}:junit-3.7.jar org.pokersource.Test
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
        
        set_tests_properties(test_java_binding PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{LD_LIBRARY_PATH}"
        )
    endif()
else()
    message(WARNING "Java or JNI not found, Java binding will not be built")
endif()