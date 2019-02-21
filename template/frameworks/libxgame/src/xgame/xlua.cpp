#include "xgame/xlua.h"
#include "xgame/xruntime.h"
#include "xgame/xfilesystem.h"

#include "cocos2d.h"

#define XLUA_MAPPING_TABLE "xlua_mapping_table"

USING_NS_CC;
USING_NS_XGAME;

static int _coroutine_resume(lua_State *L)
{
    lua_pushvalue(L, 1);
    lua_insert(L, 1);
    
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 2);
    lua_call(L, lua_gettop(L) - 2, LUA_MULTRET);
    
    if (lua_toboolean(L, 2) == 0) {
        lua_pushcfunction(L, xlua_errorfunc);
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
    }
    
    return lua_gettop(L) - 1;
}

static int xlua_fixcoresume(lua_State *L)
{
    lua_getglobal(L, LUA_COLIBNAME);
    lua_getfield(L, -1, "resume");
    lua_pushcclosure(L, _coroutine_resume, 1);
    lua_setfield(L, -2, "resume");
    lua_pop(L, 1);
    return 0;
}

static int xlua_trackback(lua_State *L)
{
    lua_pushcfunction(L, xlua_errorfunc);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "__TRACEBACK__");
    return 0;
}

static int _print(lua_State *L)
{
    int n = lua_gettop(L);
    luaL_Buffer buffer;
    lua_getglobal(L, "tostring");
    luaL_buffinit(L, &buffer);
    for (int i = 1; i <= n; i++) {
        const char *s;
        size_t l;
        lua_pushvalue(L, n + 1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        s = lua_tolstring(L, -1, &l);
        
        if (luaL_testudata(L, -2, "LUABOX")) {
            lua_insert(L, -2);
        }
        
        if (s == NULL) {
            return luaL_error(L, "'tostring' must return a string to 'print'");
        }
        
        if (i > 1) {
            luaL_addstring(&buffer, "\t");
        }
        
        luaL_addlstring(&buffer, s, l);
        
        if (luaL_testudata(L, -1, "LUABOX")) {
            lua_remove(L, -2);
        } else {
            lua_pop(L, 1);  /* pop str */
        }
    }
    luaL_pushresult(&buffer);
    
    runtime::log("%s", lua_tostring(L, -1));
    
    return 0;
}

static int xlua_fixprint(lua_State *L)
{
    lua_pushcfunction(L, _print);
    lua_setglobal(L, "print");
    return 0;
}

static int xlua_addsearchpath(lua_State *L)
{
    lua_getglobal(L, LUA_LOADLIBNAME);
    lua_getfield(L, -1, "path");
    const char *path = lua_tostring(L, -1);
    lua_pushfstring(L, "src/?.lua;?.lua;%s", path);
    lua_setfield(L, -3, "path");
    lua_pop(L, 2);
    return 0;
}

static const char *_pushnexttemplate (lua_State *L, const char *path) {
    const char *l;
    while (*path == ';') path++;
    if (*path == '\0') return NULL;
    l = strchr(path, ';');
    if (l == NULL) l = path + strlen(path);
    lua_pushlstring(L, path, l - path);
    return l;
}

static int _loader (lua_State *L)
{
    runtime::log("require \"%s\"", luaL_checkstring(L, 1));
    lua_getglobal(L, LUA_LOADLIBNAME);
    lua_getfield(L, -1, "path");
    
    const char *path = luaL_checkstring(L, -1);
    const char *name = luaL_gsub(L, luaL_checkstring(L, 1), ".", LUA_DIRSEP);
    
    lua_pushnil(L); // not found will return nil
    
    while ((path = _pushnexttemplate(L, path)) != nullptr) {
        const char *filename = luaL_gsub(L, lua_tostring(L, -1), "?", name);
        if (filesystem::exist(filename)) {
            Data buffer = FileUtils::getInstance()->getDataFromFile(filename);
            
            lua_pushfstring(L, "%s%s", "@", filename);
            int status = luaL_loadbuffer(L, (const char *)buffer.getBytes(),
                                         (size_t)buffer.getSize(), lua_tostring(L, -1));
            if (status != LUA_OK) {
                return luaL_error(L, "error loading module '%s' from file '%s':\n\t%s",
                                  lua_tostring(L, 1), filename, lua_tostring(L, -1));
            }
            
            lua_pushstring(L, filename);
            return 2;
        } else {
            lua_pop(L, 2); // path template, filename
        }
    }
    return 1;
}

static int xlua_addlualoader(lua_State *L)
{
    lua_getglobal(L, LUA_LOADLIBNAME);                          // L: package
    lua_getfield(L, -1, "searchers");                           // L: package, searchers
    
    for (int i = (int)(lua_rawlen(L, - 1) + 1); i > 2; i--) {
        lua_rawgeti(L, -1, i - 1);                              // L: package, searchers func
        lua_rawseti(L, -2, i);                                  // L: package, searchers
    }
    
    lua_pushcfunction(L, _loader);                              // L: package, searchers, func
    lua_rawseti(L, -2, 2);                                      // L: package, searchers
    
    return 0;
}

lua_State *xlua_new()
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    xlua_call(L, xlua_fixcoresume);
    xlua_call(L, xlua_fixprint);
    xlua_call(L, xlua_trackback);
    xlua_call(L, xlua_addsearchpath);
    xlua_call(L, xlua_addlualoader);
    
#ifdef COCOS2D_DEBUG
    lua_pushboolean(L, true);
#else
    lua_pushboolean(L, false);
#endif
    lua_setglobal(L, "DEBUG");
    
    return L;
}

lua_State *xlua_mainthread(lua_State *L)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    lua_State *GL = lua_tothread(L, -1);
    lua_pop(L, 1);
    return GL;
}

lua_State *xlua_cocosthread()
{
    return runtime::luaVM();
}

bool xlua_optboolean(lua_State *L, int idx, bool default_value)
{
    return lua_type(L, idx) <= LUA_TNIL ? default_value : lua_toboolean(L, idx) != 0;
}

void xlua_setnilfield(lua_State *L, const char *field)
{
    lua_pushstring(L, field);
    lua_pushnil(L);
    lua_rawset(L, -3);
}

void xlua_setfunc(lua_State *L, const char *field, lua_CFunction func)
{
    lua_pushstring(L, field);
    lua_pushcfunction(L, func);
    lua_rawset(L, -3);
}

void xlua_preload(lua_State *L, const char *name, lua_CFunction func)
{
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);  // L: preload
    lua_pushcfunction(L, func);                                 // L: preload func
    lua_setfield(L, -2, name);                                  // L: preload
    lua_pop(L, 1);                                              // L:
}

static char *simplify_traceback(const char *msg)
{
    const char *FUNCSTR = ": in function '";
    size_t len = strlen(msg);
    size_t funclen = strlen(FUNCSTR);
    int count = 0;
    char *buffer = (char *)malloc(len + 1);
    const char *end = msg + len;
    while (msg < end)
    {
        const char *funcstart = strstr(msg, FUNCSTR);
        if (funcstart)
        {
            funcstart += funclen;
            memcpy(buffer + count, msg, funcstart - msg);
            count += funcstart - msg;
            msg = funcstart;
            
            const char *funcend = strchr(msg, '\'');
            if (funcend)
            {
                funcend++;
                const char *dotstart = msg;
                while (dotstart < funcend)
                {
                    if (*dotstart++ == '.')
                        msg = dotstart;
                }
                memcpy(buffer + count, msg, funcend - msg);
                count += funcend - msg;
                msg = funcend;
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
    
    if (msg < end)
    {
        memcpy(buffer + count, msg, end - msg);
        count += end - msg;
    }
    
    buffer[count] = '\0';
    
    return buffer;
}

int xlua_errorfunc(lua_State *L)
{
    const char *errmsg = NULL;
    const char *errstack1 = NULL;
    const char *errstack2 = NULL;
    
    if (lua_isthread(L, 1)) {
        errmsg = luaL_optstring(L, 2, "");
        lua_getglobal(L, "debug");
        lua_getfield(L, -1, "traceback");
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        errstack1 = simplify_traceback(luaL_gsub(L, luaL_optstring(L, -1, ""), LUA_DIRSEP, "/"));
    } else {
        errmsg = lua_tostring(L, 1);
    }
    
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushnil(L);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    errstack2 = simplify_traceback(luaL_gsub(L, luaL_optstring(L, -1, ""), LUA_DIRSEP, "/"));
    
    if (errmsg == NULL) {
        errmsg = "";
    }
    
    luaL_Buffer buffer;
    
    //send error message to server
    luaL_buffinit(L, &buffer);
    luaL_addstring(&buffer, errmsg);
    luaL_addchar(&buffer, '\n');
    if (errstack1 != NULL) {
        luaL_addstring(&buffer, errstack1);
        luaL_addchar(&buffer, '\n');
        free((void *)errstack1);
        errstack1 = NULL;
    }
    luaL_addstring(&buffer, errstack2);
    luaL_pushresult(&buffer);
    free((void *)errstack2);
    errstack2 = NULL;
    
    const char *errinfo = lua_tostring(L, -1);
    if (strendwith(errinfo, "stack traceback:")) {
        lua_pushstring(L, runtime::getNativeStackTrace().c_str());
        errinfo = lua_tostring(L, -1);
    }
    
    runtime::reportError(errmsg, errinfo + strlen(errmsg) + 1);
    runtime::log("--------------------LUA ERROR--------------------\n%s", errinfo);
    
    return 0;
}

int xlua_pcall(lua_State *L, int n, int r)
{
    int errfunc, status;
    lua_pushcfunction(L, xlua_trackback);       // L: func arg1 ... argN errfunc
    errfunc = lua_absindex(L, -(n + 1 + 1));    // n(args) + 1(func) + 1(errfunc)
    lua_insert(L, errfunc);                     // L: errfunc func arg1 ... argN
    status = lua_pcall(L, n, r, errfunc);       // L: errfunc ret1 ... retN
    lua_remove(L, errfunc);                     // L: ret1 ... retN
    return status;
}

void xlua_call(lua_State *L, lua_CFunction func)
{
    int top = lua_gettop(L);
    lua_pushcfunction(L, func);
    lua_call(L, 0, 0);
    lua_settop(L, top);
}

int xlua_dofile(lua_State *L, const char *filename)
{
    int errfunc, status;
    
    lua_pushcfunction(L, xlua_errorfunc);               // L: errfunc
    errfunc = lua_gettop(L);
    
    luaL_gsub(L, filename, ".lua", "");                 // L: errfunc "xxx.xxxx"
    luaL_gsub(L, lua_tostring(L, -1), "/", ".");        // L: errfunc "xxx.xxxx" "xxx/xxxx"
    lua_remove(L, -2);                                  // L: errfunc "xxx/xxxx"
    
    lua_pushcfunction(L, _loader);                      // L: errfunc "xxx/xxxx" _loader
    lua_pushvalue(L, -2);                               // L: errfunc "xxx/xxxx" _loader "xxx/xxxx"
    
    status = lua_pcall(L, 1, LUA_MULTRET, errfunc);     // L: errfunc "xxx/xxxx" func filename
    lua_remove(L, errfunc + 1);                         // L: errfunc func filename
    
    if (status == LUA_OK) {
        if (lua_isfunction(L, errfunc + 1)) {
            status = lua_pcall(L, 1, LUA_MULTRET, errfunc);
            lua_remove(L, errfunc);
        } else {
            status = LUA_ERRFILE;
            lua_settop(L, errfunc - 1);
            runtime::log("[NO] do file: no such file '%s'", filename);
        }
    } else {
        lua_settop(L, errfunc - 1);
        runtime::log("do file error: '%s'", filename);
    }
    
    return status;
}

static void xlua_getmappingtable(lua_State *L)
{
    lua_pushstring(L, XLUA_MAPPING_TABLE);
    if (lua_rawget(L, LUA_REGISTRYINDEX) == LUA_TNIL) {
        lua_pop(L, 1); // pop nil
        lua_newtable(L);
        lua_pushstring(L, XLUA_MAPPING_TABLE);
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
    luaL_checktype(L, -1, LUA_TTABLE);
}

int xlua_ref(lua_State *L, int idx)
{
    static int ref_count = 0;
    
    int ref = LUA_REFNIL;
    idx = lua_absindex(L, idx);
    if (!lua_isnil(L, idx)) {
        ref = ++ref_count;
        xlua_getmappingtable(L);
        lua_pushvalue(L, idx);
        lua_rawseti(L, -2, ref);
        lua_pop(L, 1); // pop mapping table
    }
    
    return ref;
}

int xlua_reffunc(lua_State *L, int idx)
{
    luaL_checktype(L, idx, LUA_TFUNCTION);
    return xlua_ref(L, idx);
}

void xlua_unref(lua_State *L, int ref)
{
    int top = lua_gettop(L);
    xlua_getmappingtable(L);
    if (lua_rawgeti(L, top + 1, ref) != LUA_TNIL) {
        lua_pushnil(L);
        lua_rawseti(L, top + 1, ref);
    }
    lua_settop(L, top);
}

void xlua_getref(lua_State *L, int ref)
{
    xlua_getmappingtable(L);
    lua_rawgeti(L, -1, ref);
    lua_insert(L, -2);
    lua_pop(L, 1); // pop mapping table
}

const char *xluaf_getstring(lua_State *L, int idx, const char *field, const char *default_value)
{
    lua_getfield(L, idx, field);
    const char *value = luaL_optstring(L, -1, default_value);
    lua_pop(L, 1);
    return value;
}

bool xluaf_getboolean(lua_State *L, int idx, const char *field, bool default_value)
{
    lua_getfield(L, idx, field);
    bool value = xlua_optboolean(L, -1, default_value);
    lua_pop(L, 1);
    return value;
}

lua_Number xluaf_getnumber(lua_State *L, int idx, const char *field, lua_Number default_value)
{
    lua_getfield(L, idx, field);
    float value = luaL_optnumber(L, -1, default_value);
    lua_pop(L, 1);
    return value;
}

lua_Integer xluaf_getinteger(lua_State *L, int idx, const char *field, lua_Integer default_value)
{
    lua_getfield(L, idx, field);
    lua_Integer value = luaL_optinteger(L, -1, default_value);
    lua_pop(L, 1);
    return value;
}

const char *xluaf_checkstring(lua_State *L, int idx, const char *field)
{
    lua_getfield(L, idx, field);
    const char *value = luaL_checkstring(L, -1);
    lua_pop(L, 1);
    return value;
}

lua_Number xluaf_checknumber(lua_State *L, int idx, const char *field)
{
    lua_getfield(L, idx, field);
    float value = (float) luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return value;
}

lua_Integer xluaf_checkinteger(lua_State *L, int idx, const char *field)
{
    lua_getfield(L, idx, field);
    lua_Integer value = luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return value;
}