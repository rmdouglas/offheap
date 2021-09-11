
#include <stddef.h>
#include <stdio.h>
#include "caml/misc.h"
#include "caml/mlvalues.h"
#include "caml/domain_state.h"

typedef struct
{
#ifdef CAML_NAME_SPACE
#define DOMAIN_STATE(type, name) CAMLalign(8) type name;
#else
#define DOMAIN_STATE(type, name) CAMLalign(8) type _##name;
#endif
DOMAIN_STATE(void *, young_base)
DOMAIN_STATE(value *, young_start)
DOMAIN_STATE(value *, young_end)
DOMAIN_STATE(value *, young_alloc_start)
DOMAIN_STATE(value *, young_alloc_end)
DOMAIN_STATE(value *, young_alloc_mid)
DOMAIN_STATE(value *, young_trigger)
DOMAIN_STATE(asize_t, minor_heap_wsz)
DOMAIN_STATE(intnat, in_minor_collection)
DOMAIN_STATE(double, extra_heap_resources_minor)
DOMAIN_STATE(struct caml_ref_table *, ref_table)
DOMAIN_STATE(struct caml_ephe_ref_table *, ephe_ref_table)
DOMAIN_STATE(struct caml_custom_table *, custom_table)
#undef DOMAIN_STATE
} caml_region_state;

#define Caml_region_field(region, field) region->_##field

CAMLexport
caml_region_state* caml_region_swap(caml_region_state *next_region) {
    caml_region_state *prev_region = (caml_region_state*)malloc(sizeof(caml_region_state));

    Caml_region_field(prev_region, young_base) = Caml_state_field(young_base);
    Caml_region_field(prev_region, young_start) = Caml_state_field(young_start);
    Caml_region_field(prev_region, young_end) = Caml_state_field(young_end);
    Caml_region_field(prev_region, young_alloc_start) = Caml_state_field(young_alloc_start);
    Caml_region_field(prev_region, young_alloc_end) = Caml_state_field(young_alloc_end);
    Caml_region_field(prev_region, young_alloc_mid) = Caml_state_field(young_alloc_mid);
    Caml_region_field(prev_region, young_trigger) = Caml_state_field(young_trigger);
    Caml_region_field(prev_region, minor_heap_wsz) = Caml_state_field(minor_heap_wsz);
    Caml_region_field(prev_region, in_minor_collection) = Caml_state_field(in_minor_collection);
    Caml_region_field(prev_region, extra_heap_resources_minor) = Caml_state_field(extra_heap_resources_minor);
    Caml_region_field(prev_region, ref_table) = Caml_state_field(ref_table);
    Caml_region_field(prev_region, ephe_ref_table) = Caml_state_field(ephe_ref_table);
    Caml_region_field(prev_region, custom_table) = Caml_state_field(custom_table);

    Caml_state_field(young_base) = Caml_region_field(next_region, young_base);
    Caml_state_field(young_start) = Caml_region_field(next_region, young_start);
    Caml_state_field(young_end) = Caml_region_field(next_region, young_end);
    Caml_state_field(young_alloc_start) = Caml_region_field(next_region, young_alloc_start);
    Caml_state_field(young_alloc_end) = Caml_region_field(next_region, young_alloc_end);
    Caml_state_field(young_alloc_mid) = Caml_region_field(next_region, young_alloc_mid);
    Caml_state_field(young_trigger) = Caml_region_field(next_region, young_trigger);
    Caml_state_field(minor_heap_wsz) = Caml_region_field(next_region, minor_heap_wsz);
    Caml_state_field(in_minor_collection) = Caml_region_field(next_region, in_minor_collection);
    Caml_state_field(extra_heap_resources_minor) = Caml_region_field(next_region, extra_heap_resources_minor);
    Caml_state_field(ref_table) = Caml_region_field(next_region, ref_table);
    Caml_state_field(ephe_ref_table) = Caml_region_field(next_region, ephe_ref_table);
    Caml_state_field(custom_table) = Caml_region_field(next_region, custom_table);

    return prev_state;
}
