cmake_minimum_required(VERSION 3.28...3.30)

# 引入 OBS 官方构建脚本
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# 强制开启 UI 和前端 API 支持
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# 定义模块
add_library(${CMAKE_PROJECT_NAME} MODULE)

# 链接 OBS 核心库
find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# 链接 OBS 前端 API 库
if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

# Qt 6 配置
if(ENABLE_QT)
  # 🎯 修改：增加 Svg 组件
  find_package(Qt6 COMPONENTS Widgets Core Network Svg REQUIRED)
  
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE 
    Qt6::Core 
    Qt6::Widgets 
    Qt6::Network
    Qt6::Svg
  )
  
  # 开启 Qt 自动化工具
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES 
    AUTOMOC ON 
    AUTOUIC ON 
    AUTORCC ON
  )

  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
endif()

# 🎯 修改：加入 resources.qrc
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/resources.qrc
    
    src/plugin-main.cpp
    src/Common.h
    src/AudioController.h
    src/AudioController.cpp
    src/HttpServer.h
    src/HttpServer.cpp
    src/Dashboard.h
    src/Dashboard.cpp
    src/Dashboard.ui
    src/ConfigDialog.h
    src/ConfigDialog.cpp
    src/ConfigDialog.ui
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})