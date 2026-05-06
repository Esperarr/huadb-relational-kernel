if(NOT EMSCRIPTEN)
  # B+ 树评测验证器：与 huadb 主库源码彻底解耦（仅依赖标准库）。
  # 同时提供独立的自测 binary btree_verifier_test。
  add_library(btree_verifier OBJECT
    btree/parser.cpp
    btree/verifier.cpp
  )
  target_include_directories(btree_verifier PUBLIC btree)

  add_executable(btree_verifier_test btree/verifier_test.cpp)
  target_link_libraries(btree_verifier_test btree_verifier)

  add_executable(sqllogictest sqllogictest.cpp sqllogicparser.cpp)
  target_link_libraries(sqllogictest huadb btree_verifier)
endif()
