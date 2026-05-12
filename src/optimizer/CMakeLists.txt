if(${LAB6})
  add_definitions(-DLAB6)
endif()

add_library(
  optimizer
  OBJECT
  optimizer.cpp
  normalize_predicates.cpp
  push_down_predicates.cpp
  seq_to_index.cpp
  join_reorder.cpp
)

set(ALL_OBJECT_FILES
  ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:optimizer>
  PARENT_SCOPE)
