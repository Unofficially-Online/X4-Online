/* A cue's identity: its static cue's path "<script>.<root>.<...>.<cue>" (lockstep_cue_name.h), its key the FNV-1a 64 of the
 * encoded text, cached per (definition, module). A path the static cue's fields refuse is counted (cue_refused=) and the
 * cue is unnamed (SR_UNNAMED): its module's draw stays native, as before for a cue the name map did not find. */
static unsigned script_rng_md(script_rng_identity *id) {
    uintptr_t cue=id->local,vtable=0,definition=0,module=0;
    if(!script_rng_read((void*)cue,&vtable,8)) return SR_UNREADABLE;
    if(vtable!=(uintptr_t)(image+SCRIPT_RNG_CUE_VT)) return SR_FOREIGN;
    if(!script_rng_read((void*)(cue+CUE_PATH_DEF),&definition,8) || !definition ||
       !script_rng_read((void*)(definition+CUE_PATH_SCRIPT),&module,8) || !module) return SR_UNREADABLE;
    id->instance=definition!=cue;id->script=SCRIPT_RNG_MD_SCRIPT;
    unsigned first=(unsigned)(lockstep_mix(definition)%SCRIPT_RNG_NAMES);script_rng_name *slot=NULL;
    for(unsigned n=0;n<SCRIPT_RNG_PROBES;n++) {
        script_rng_name *e=&script_rng_names[(first+n)%SCRIPT_RNG_NAMES];
        if(e->definition==definition) {
            if(e->module==module) {id->owner=e->hash;memcpy(id->name,e->name,sizeof(id->name));return SR_OK;}
            slot=e;break; /* Same address, other module: a replaced definition. */
        }
        if(!e->definition) {slot=e;break;}
    }
    /* A miss reads the static cue's levels up to its root with the guarded copy, not one system call per read. */
    char full[CUE_PATH_TEXT];cue_path path;
    if(!cue_path_with(script_rng_read,(uintptr_t)(image+SCRIPT_RNG_CUE_VT),definition,full,sizeof(full),&path)) {
        cue_path_refused(&path);return SR_UNNAMED;
    }
    const uint64_t hash=cue_fnv(CUE_FNV_BASIS,full,path.length);
    size_t n=path.length;if(n>=sizeof(id->name)) n=sizeof(id->name)-1;memcpy(id->name,full,n);id->name[n]=0;
    id->owner=hash;
    if(slot) {
        if(!slot->definition) ++script_rng_name_count;
        slot->definition=definition;slot->module=module;slot->hash=hash;memcpy(slot->name,id->name,sizeof(slot->name));
    }
    return SR_OK;
}
