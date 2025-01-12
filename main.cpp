#ifndef DONT_USE_STB
    #include <mod/thirdparty/stb_sprintf.h>
    #define sprintf stbsp_sprintf
    #define snprintf stbsp_snprintf
#endif
#include <include/jnifn.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h> // mkdir
#include <sys/sendfile.h> // sendfile
#include <fcntl.h> // "open" flags
#include <dlfcn.h>

#include <include/aml.h>
#include <include/defines.h>
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#ifdef __IL2CPPUTILS
    #include <il2cpp/functions.h>
#endif

// Should be after config.h in main.cpp
#include <icfg_desc.h>
// Should be after config.h in main.cpp

#include <include/interfaces.h>
#include <include/modslist.h>

char g_szInternalStoragePath[256],
     g_szAppName[256],
     g_szFakeAppName[256],
     g_szModsDir[256],
     g_szInternalModsDir[256],
     g_szAndroidDataDir[256],
     g_szCfgPath[256];
const char* g_szDataDir;

static ModInfo modinfoLocal("net.rusjj.aml", "AML Core", "1.0.1", "RusJJ aka [-=KILL MAN=-]");
ModInfo* modinfo = &modinfoLocal;
static Config cfgLocal("ModLoaderCore");
Config* cfg = &cfgLocal;
static CFG icfgLocal; ICFG* icfg = &icfgLocal;

inline size_t __strlen(const char *str)
{
    const char* s = str;
    while(*s) ++s;
    return (s - str);
}

inline bool EndsWith(const char* base, const char* str)
{
    static int blen, slen;
    blen = strlen(base);
    slen = strlen(str);
    return (blen >= slen) && (!strcmp(base + blen - slen, str));
}

inline bool EndsWithSO(const char* base)
{
    static int blen;
    blen = strlen(base);
    return (blen >= 3) && (!strcmp(base + blen - 3, ".so"));
}

// Is this actually faster, bruh?
// P.S. Yeah, it is!
inline bool CopyFileFaster(const char* file, const char* dest)
{
    int inFd = open(file, O_RDONLY);
    if(inFd < 0) return false;
    struct stat statBuf;
    fstat(inFd, &statBuf);
    int outFd = open(dest, O_WRONLY | O_CREAT, statBuf.st_mode);
    if(outFd < 0)
    {
        close(inFd);
        return false;
    }
    if(sendfile(outFd, inFd, NULL, statBuf.st_size) < 0)
    {
        close(inFd);
        close(outFd);
        return false;
    }
    close(inFd);
    close(outFd);
    return true;
}

inline bool CopyFile(const char* file, const char* dest)
{
    FILE* source = fopen(file, "r");
    if(source == NULL) return false;
    FILE* target = fopen(dest, "w");
    if(target == NULL) 
    {
        fclose(source);
        return false;
    }
    while(!feof(source)) fputc(fgetc(source), target);
    fclose(source);
    fclose(target);
    return true;
}

inline bool HasFakeAppName()
{
    return (g_szFakeAppName[0] != 0 && strlen(g_szFakeAppName) > 5);
}

typedef const char* (*SpecificGameFn)();
void LoadMods(const char* path)
{
    ModInfo* pModInfo = NULL;
    SpecificGameFn maybeINeedAGame = NULL;
    GetModInfoFn modInfoFn = NULL;

    char buf[256], dataBuf[256];
    DIR* dir = opendir(path);
    if (dir != NULL)
    {
        logger->Info("Opening %s", path);
        struct dirent *diread; void* handle;
        const char* gameName = HasFakeAppName() ? g_szFakeAppName : g_szAppName;
        while ((diread = readdir(dir)) != NULL)
        {
            if(diread->d_name[0] == '.') continue; // Skip . and ..
            if(!EndsWithSO(diread->d_name))
            {
                // Useless info for us!
                //logger->Error("File %s is not a mod, atleast it is NOT .SO file!", diread->d_name);
                continue;
            }
            snprintf(buf, sizeof(buf), "%s/%s", path, diread->d_name);
            snprintf(dataBuf, sizeof(dataBuf), "%s/%s", g_szDataDir, diread->d_name);
            //unlink(dataBuf);
            chmod(dataBuf, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP); // XMDS
            int removeStatus = remove(dataBuf);
            //if(removeStatus != 0) logger->Error("Failed to remove temporary mod file! This may broke the mod loading! Error %d", removeStatus);
            if(!CopyFileFaster(buf, dataBuf) && !CopyFile(buf, dataBuf))
            {
                logger->Error("File %s is failed to be copied! :(", diread->d_name);
                continue;
            }
            chmod(dataBuf, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP);

            handle = dlopen(dataBuf, RTLD_NOW); // Load it to RAM!
            modInfoFn = (GetModInfoFn)dlsym(handle, "__GetModInfo");
            if(modInfoFn != NULL)
            {
                pModInfo = modInfoFn();
                maybeINeedAGame = (SpecificGameFn)dlsym(handle, "__INeedASpecificGame");
                if(maybeINeedAGame != NULL && strcmp(maybeINeedAGame(), gameName) != 0)
                {
                    logger->Error("Mod (GUID %s) built for the game %s!", pModInfo->GUID(), maybeINeedAGame());
                    goto nextMod;
                }
                if(!modlist->AddMod(pModInfo, handle))
                {
                    logger->Error("Mod (GUID %s) is already loaded!", pModInfo->GUID());
                    goto nextMod;
                }
                logger->Info("Mod (GUID %s) has been preloaded...", pModInfo->GUID());
            }
            else
            {
              nextMod:
                dlclose(handle);
            }
            //unlink(dataBuf);
            removeStatus = remove(dataBuf);
            if(removeStatus != 0) logger->Error("Failed to remove temporary mod file! This may broke the mod loading! Error %d", removeStatus);
        }
        closedir(dir);
    }
    else
    {
        logger->Error("Failed to load mods: DIR IS NOT OPEN");
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    logger->SetTag("AndroidModLoader");
    const char* szTmp; jstring jTmp;

    /* JNI Environment */
    JNIEnv* env = NULL;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
    {
        logger->Error("Cannot get JNI Environment!");
        return -1;
    }

    /* Application Context */
    jobject appContext = GetGlobalContext(env);
    if(appContext == NULL)
    {
        logger->Error("AML Library should be loaded in \"onCreate\" or by injecting it directly into the main game library!");
        return JNI_VERSION_1_6;
    }

    /* Must Have for mods */    
    interfaces->Register("AMLInterface", aml);
    interfaces->Register("AMLConfig", icfg);
    modlist->AddMod(modinfo, 0);

    /* Permissions! We really need them for configs! */
    /*if(!HasPermissionGranted(env, appContext, "READ_EXTERNAL_STORAGE") ||
       !HasPermissionGranted(env, appContext, "WRITE_EXTERNAL_STORAGE"))
    {
        // Instead of appContext should be !!!ACTIVITY!!! <- Hard to get without SMALI-Inject (just a smali hand-rewritten, lol)
        RequestPermissions(env, appContext);
    }*/

    /* Internal Storage */
    jTmp = GetAbsolutePath(env, GetStorageDir(env));
    szTmp = env->GetStringUTFChars(jTmp, NULL);
    snprintf(g_szInternalStoragePath, sizeof(g_szInternalStoragePath), "%s", szTmp);
    env->ReleaseStringUTFChars(jTmp, szTmp);

    /* Package Name */
    char i = 0;
    jTmp = GetPackageName(env, appContext);
    szTmp = env->GetStringUTFChars(jTmp, NULL);
    while(szTmp[i] != 0 && i < sizeof(g_szAppName)-1)
    {
        g_szAppName[i] = tolower(szTmp[i]);
        ++i;
    } g_szAppName[i] = 0;
    env->ReleaseStringUTFChars(jTmp, szTmp);
    logger->Info("Determined app info: %s", g_szAppName);

    /* Create a folder in /Android/data/.../ */
    char szBuf[256];
    snprintf(szBuf, sizeof(szBuf), "%s/Android/data/%s/", g_szInternalStoragePath, g_szAppName);
    DIR* dir = opendir(szBuf);
    if(dir != NULL) closedir(dir);
    else GetExternalFilesDir(env, appContext);

    /* Create "mods" folder in /Android/data/.../ */
    snprintf(g_szModsDir, sizeof(g_szModsDir), "%s/Android/data/%s/mods/", g_szInternalStoragePath, g_szAppName);
    mkdir(g_szModsDir, 0777);

    /* Create "files" folder in /Android/data/.../ */
    snprintf(g_szAndroidDataDir, sizeof(g_szAndroidDataDir), "%s/Android/data/%s/files/", g_szInternalStoragePath, g_szAppName);
    mkdir(g_szAndroidDataDir, 0777); // Who knows, right?

    /* Create "configs" folder in /Android/data/.../ */
    snprintf(g_szCfgPath, sizeof(g_szCfgPath), "%s/Android/data/%s/configs/", g_szInternalStoragePath, g_szAppName);
    mkdir(g_szCfgPath, 0777);

    /* root/data/data Folder */
    g_szDataDir = env->GetStringUTFChars(GetAbsolutePath(env, GetFilesDir(env, appContext)), NULL);

    /* AML Config */
    logger->Info("Reading config...");
    cfg->Init();
    cfg->BindOnce("Author", "")->SetString("RusJJ aka [-=KILL MAN=-]");
    cfg->BindOnce("Discord", "")->SetString("https://discord.gg/2MY7W39kBg");
    bool bHasChangedCfgAuthor = cfg->IsValueChanged();
    cfg->BindOnce("Version", "")->SetString(modinfo->VersionString());
    cfg->BindOnce("LaunchedTimeStamp", 0)->SetInt((int)time(NULL));
    cfg->BindOnce("FakePackageName", "")->GetString(g_szFakeAppName, sizeof(g_szFakeAppName));
    snprintf(g_szInternalModsDir, sizeof(g_szInternalModsDir), "%s/%s/%s", g_szInternalStoragePath, cfg->BindOnce("InternalModsFolder", "AMLMods")->GetString(), g_szAppName);
    bool internalModsPriority = cfg->BindOnce("InternalModsFirst", true)->GetBool();
    logger->ToggleOutput(cfg->BindOnce("EnableLogcats", true)->GetBool());
    cfg->Save();

    /* Mods? */
    logger->Info("Working with mods...");
    #ifdef __IL2CPPUTILS
        logger->Info("IL2CPP: Attempting to initialize IL2CPP-Utils");
        IL2CPP::Func::HookFunctions();
    #endif
    LoadMods(internalModsPriority ? g_szInternalModsDir : g_szModsDir);
    LoadMods(internalModsPriority ? g_szModsDir : g_szInternalModsDir);

    /* All mods are loaded now. We should check for dependencies! */
    logger->Info("Checking for dependencies...");
    modlist->ProcessDependencies();
    
    /* Process features */
    #ifdef __XDL
        g_pAML->AddFeature("XDL");
    #endif
    #ifdef __IL2CPPUTILS
        g_pAML->AddFeature("IL2CPP");
    #endif
    if(g_pAML->IsGameFaked()) g_pAML->AddFeature("FAKEGAME");
    if(bHasChangedCfgAuthor) g_pAML->AddFeature("STEALER");
    if(!logger->HasOutput()) g_pAML->AddFeature("NOLOGGING");

    /* All mods are sorted and should be loaded! */
    modlist->ProcessPreLoading();
    modlist->ProcessLoading();
    modlist->OnAllModsLoaded();
    logger->Info("Mods were launched!");

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM* vm, void* reserved)
{
    /* Not sure if it'll work... */
    modlist->ProcessUnloading();
}
