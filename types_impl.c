#include <assert.h>
#include <stdio.h>
#include "common.h"
#include "types_impl.h"
#include "vendor/klib/khash.h"

KHASH_MAP_INIT_INT(SymbolTableMap, void *)
KHASH_MAP_INIT_INT(MemberMap, const Member *)

typedef struct SymbolTable {
  kh_SymbolTableMap_t *map;
  struct SymbolTable *parent;
} SymbolTable;

Value *new_value(const Type *type, void *value) {
  Value *ret = checked_calloc(1, sizeof(*ret));
  *ret = (Value) {
    .type = type,
    .value = value,
  };
  return ret;
}

Member *new_member(const Type *type, int offset, const char *ident, int ident_id) {
  Member *ret = checked_calloc(1, sizeof(*ret));
  *ret = (Member) {
    .type = type,
    .offset = offset,
    .ident = ident,
    ._ident_id = ident_id,
  };
  return ret;
}

SymbolTable *new_symbol_table() {
  SymbolTable *ret = checked_calloc(1, sizeof(*ret));
  ret->map = kh_init_SymbolTableMap();
  return ret;
}

void *new_type_map() {
  return kh_init_SymbolTableMap();
}

static void *lookup_symbol(const SymbolTable *tab, int string_id) {
  if (!tab)
    return 0;
    
  khiter_t iter = kh_get_SymbolTableMap(tab->map, string_id);
  if (iter == kh_end(tab->map))  // not found; to go parent
    return lookup_symbol(tab->parent, string_id);

  return kh_val(tab->map, iter);
}

Value *lookup_value(const SymbolTable *tab, int string_id) {
  return lookup_symbol(tab, string_id);
}

Type *lookup_type(const SymbolTable *tab, int string_id) {
  return lookup_symbol(tab, string_id);
}

Type *lookup_type_norecur(const SymbolTable *tab, int string_id) {
  assert(tab->map);
  khiter_t iter = kh_get_SymbolTableMap(tab->map, string_id);
  if (iter == kh_end(tab->map))  // not found
    return 0;

  return kh_val(tab->map, iter);
}

void push_symbol_table(SymbolTable **p_symtab) {
  SymbolTable *next = new_symbol_table();
  next->parent = *p_symtab;
  *p_symtab = next;
}

void pop_symbol_table(SymbolTable **p_symtab) {
  // worry about freeing later
  *p_symtab = (*p_symtab)->parent;
}

/** Insert a symbol that does not already exist into tab; will throw EXC_PARSE_SYNTAX if key ident_string_id exists. */
void insert_symbol(SymbolTable *tab, int ident_string_id, void *symbol) {
  DEBUG_PRINT_EXPR("%d inserted into %p", ident_string_id, (void *) tab->map);
  khiter_t iter = kh_get_SymbolTableMap(tab->map, ident_string_id);
  THROWF_IF(iter != kh_end(tab->map), EXC_PARSE_SYNTAX, "symbol ident_string_id %d is already defined in current scope", ident_string_id);
  int ret;
  iter = kh_put_SymbolTableMap(tab->map, ident_string_id, &ret);
  THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed");

  kh_val(tab->map, iter) = symbol;

  // debug
  khiter_t get_iter = kh_get_SymbolTableMap(tab->map, ident_string_id);
  THROWF_IF(
    get_iter == kh_end(tab->map),
    EXC_INTERNAL,
    "could not get ident_string_id %d back from the symbol map hashtable", ident_string_id
  );
  void *got_symbol = kh_val(tab->map, get_iter);
  assert(symbol == got_symbol);
}

void *new_member_map() {
  return kh_init_MemberMap();
}

void insert_member(void *member_map, const Member *member) {
  kh_MemberMap_t *member_map_impl = member_map;
  khiter_t iter = kh_get_MemberMap(member_map_impl, member->_ident_id);
  THROWF_IF(
    iter != kh_end(member_map_impl),
    EXC_PARSE_SYNTAX,
    "member with string id %d already defined", member->_ident_id
  );
  int ret;
  iter = kh_put_MemberMap(member_map_impl, member->_ident_id, &ret);
  THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed");

  kh_val(member_map_impl, iter) = member;
}

const Member *lookup_member(const Type *struct_type, int ident) {
  assert(struct_type->kind == TY_STRUCT || struct_type->kind == TY_UNION);
  const kh_MemberMap_t *member_map_impl = struct_type->member_map;
  khiter_t iter = kh_get_MemberMap(member_map_impl, ident);
  THROWF_IF(
    iter == kh_end(member_map_impl),
    EXC_PARSE_SYNTAX,
    "member with string id %d NOT defined", ident
  );
  return kh_val(member_map_impl, iter);
}

void print_json_kv_str(FILE *out, const char *key, const char *value) {
  fprintf(out, "\"%s\":\"%s\",", key, value);
}
#define print_json_kv(out, key, format, value) fprintf(out, "\"%s\":" format ",", key, value)

void type_to_json_recur(FILE *out, const Type *type) {
  fputc('{', out);
  print_json_kv_str(out, "_type", "Type");
  switch (type->kind) {
    case TY_VOID:
      print_json_kv_str(out, "kind", "TY_VOID");
      break;
    case TY_INTEGER:
      print_json_kv_str(out, "kind", "TY_INTEGER");
      print_json_kv(out, "size", "%d", type->size);
      print_json_kv(out, "is_unsigned", "%d", type->is_unsigned);
      break;
    case TY_FLOAT:
      print_json_kv_str(out, "kind", "TY_FLOAT");
      print_json_kv(out, "size", "%d", type->size);
      break;
    default:
      THROWF(EXC_INTERNAL, "type kind %d not supported", type->kind)
  }
  fputc('}', out);
}

char *type_to_json(const Type *type) {
  char *buf;
  size_t size;
  FILE *out = checked_open_memstream(&buf, &size);
  type_to_json_recur(out, type);
  checked_fclose(out);
  return buf;
}

static int is_compatible_member(const Member *m1, const Member *m2) {
  assert(0 && "Unimplemented!");
}

int is_compatible_complete_type(Type *t1, Type *t2) {
  assert(IS_COMPLETE_TYPE(t1) && IS_COMPLETE_TYPE(t2));
  int ret = t1->kind == t2->kind && t1->size == t2->size && t1->align == t2->align 
    && t1->qualifiers.is_const == t2->qualifiers.is_const
    && t1->qualifiers.is_restrict == t2->qualifiers.is_restrict
    && t1->qualifiers.is_volatile == t2->qualifiers.is_volatile;

  if (!ret) return ret;
    
  if (IS_PRIMITIVE_TYPE(t1)) {
    ret = ret && t1->is_unsigned;
  } else if (t1->kind == TY_POINTER) {
    // TODO: Make all types const!
    ret = ret && get_composite_type((Type *) t1->child_type, (Type *) t2->child_type);
  } else if (t1->kind == TY_STRUCT || t1->kind == TY_UNION) {
    ret = ret && t1->n_members == t2->n_members;
    if (!ret) return ret;

    for (int i = 0; i < t1->n_members; i++) {
      ret = ret && is_compatible_member(t1->members[i], t2->members[i]);
      if (!ret) return ret;
    }
  } else {
    THROWF(EXC_INTERNAL, "Type kind %d unimplemented", t1->kind);
  }
  return ret;
}

/**
 * Return a composite type compatible with both t1 and t2, or NULL if not possible.
 */
Type *get_composite_type(Type *t1, Type *t2) {
  if (t1->kind != t2->kind) return 0;

  if (IS_COMPLETE_TYPE(t1) && IS_COMPLETE_TYPE(t2)) {
    if (is_compatible_complete_type(t1, t2)) {
      return t1;
    } else {
      return 0;
    }
  }
  
  Type *complete_type = 0, *incomplete_type = 0;
  if (IS_COMPLETE_TYPE(t1)) {
    complete_type = t1; 
    incomplete_type = t2;
  } else if (IS_COMPLETE_TYPE(t2)) {
    complete_type = t1;
    incomplete_type = t2;
  }

  if (IS_TAGGED_TYPE(complete_type)) {
    if (complete_type->tag == incomplete_type->tag) {
      return complete_type;
    }
    return 0;
  }

  assert(0 && "Unimplemented!");
}

Type VOID_TYPE = { .kind = TY_VOID };
