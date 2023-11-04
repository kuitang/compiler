/** Type implementations, including Symbol tables and member maps. Should only be used by the parser. */

#pragma once
#include "types.h"

typedef struct SymbolTable SymbolTable;

// TODO: get rid of this; we use type_of anyways.
typedef struct Value {
  const Type *type;
  void *value;
} Value;

Value *new_value(const Type *type, void *value);
Member *new_member(const Type *type, int offset, const char *ident, int ident_id);

SymbolTable *new_symbol_table();
Value *lookup_value(const SymbolTable *tab, int string_id);
Type *lookup_type(const SymbolTable *tab, int string_id);
Type *lookup_type_norecur(const SymbolTable *tab, int string_id);
void insert_symbol(SymbolTable *tab, int string_id, void *value);
void push_symbol_table(SymbolTable **p_tab);
void pop_symbol_table(SymbolTable **p_tab);

void *new_member_map();
const Member *lookup_member(const Type *struct_type, int ident);
/**
 * Insert member into member_map of a struct.
 * @throw EXC_PARSE_SYNTAX if a member with the same name has already been defined
 * @throw EXC_INTERNAL if hashmap failed
 */
void insert_member(void *member_map, const Member *member);
Type *get_composite_type(Type *t1, Type *t2);
