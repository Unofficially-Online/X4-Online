/* Lua surface of the UI replay's inventory family (lockstep_ui_inventory.h; tools/dev/native_gate_ui_inventory_core.lua). */
/* gate.ui_inventory(): after gate.ui_replay (with the family) and gate.ui_guard, before the first admitted tick: the
 * DropInventory entry is this family's. 1, or 0 and the reason. */
static int lua_ui_inventory(lua_State *L) {
    int ok=accept_ui() && uiinv_arm();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,uiinv_why,strlen(uiinv_why));return 2;
}
/* gate.ui_inventory_scope(1): a click's scope opens on this client: the claimed exports are recorded, not run (1, or 0 when
 * the family cannot capture now). gate.ui_inventory_scope(0): it closes; the recorded rows ("name<tab>argument...<newline>"),
 * or nil and 'lost' when a call did not fit the record (the click is refused whole). */
static int lua_ui_inventory_scope(lua_State *L) {
    double on=p_lua_tonumber(L,1);
    if(!accept_ui()) {p_lua_pushnil(L);return 1;}
    if(on==1) {p_lua_pushnumber(L,uiinv_scope_begin());return 1;}
    int lost=0;const char *rows=uiinv_scope_end(&lost);
    if(lost) {p_lua_pushnil(L);p_lua_pushlstring(L,"lost",4);return 2;}
    p_lua_pushlstring(L,rows,strlen(rows));return 1;
}
/* gate.ui_inventory_player(slot): the runtime ID (decimal) of slot's own player entity, nil when it has none now. Client
 * k's menus read "the player's" inventory in the presentation scope, whose player singleton stays slot 1's entity (live
 * inv1): the Lua half reads slot k's entity by this ID instead. Read only. */
static int lua_ui_inventory_player(lua_State *L) {
    double slot=p_lua_tonumber(L,1);uint64_t id=0;char text[24];
    const unsigned char *player=accept_ui() && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS?uirep_slot_player((unsigned)slot):NULL;
    if(!player || !read_local(player+UIREP_ID,&id,8) || !id) {p_lua_pushnil(L);return 1;}
    int n=snprintf(text,sizeof(text),"%llu",(unsigned long long)id);
    p_lua_pushlstring(L,text,(size_t)n);return 1;
}
/* gate.ui_inventory_status(): the status row. */
static int lua_ui_inventory_status(lua_State *L) {
    char text[160];int n=uiinv_status(text,sizeof(text));
    if(!accept_ui() || n<=0 || (size_t)n>=sizeof(text)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,text,(size_t)n);return 1;
}
