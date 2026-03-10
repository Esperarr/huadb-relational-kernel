add_library(
  index
  OBJECT
  btree_index.cpp
  btree_internal_page.cpp
  btree_leaf_page.cpp
	btree_meta_page.cpp
  btree_page.cpp
  index.cpp
)

set(ALL_OBJECT_FILES
  ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:index>
  PARENT_SCOPE)
