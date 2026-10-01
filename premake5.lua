-- The folder a project is deployed to, and the game it is started from when debugging,
-- is the path of one machine and does not belong in the repository. It is read from a
-- `.env` file next to this script, which is not tracked by git and holds one
-- `<KEY>=<folder>` line per game (quotes and a trailing slash are optional). A project
-- whose key is missing is not deployed at all.
local envkeys = nil
function envdir(key)
   if not envkeys then
      envkeys = {}
      local text = io.readfile(path.join(_SCRIPT_DIR, ".env")) or ""
      for line in text:gmatch("[^\r\n]+") do
         local k, v = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
         if k and v ~= "" then
            v = v:gsub('^"', ""):gsub('"$', ""):gsub("^'", ""):gsub("'$", "")
            envkeys[k] = v
         end
      end
   end

   local value = envkeys[key]
   if not value then return nil end

   value = value:gsub("[%s\\/]+$", "")
   if value == "" then return nil end

   return path.translate(value)
end

-- Write debugger settings into the project as well as its local .user file.
if _ACTION and _ACTION:match("^vs") then
   require("vstudio")
   premake.override(premake.vstudio.vc2010.elements, "outputProperties", function(base, cfg)
      local elements = base(cfg)
      table.insert(elements, premake.vstudio.vc2010.debugSettings)
      return elements
   end)
end

local games = json.decode(io.readfile(path.join(_SCRIPT_DIR, "games.json")))
newoption {
    trigger     = "with-version",
    value       = "STRING",
    description = "Current version",
}

for _, game in ipairs(games) do
   local configs = {}
   for _, store in ipairs({ "GOG", "Steam" }) do
      for _, mode in ipairs({ "Debug", "Release" }) do
         table.insert(configs, game.id .. "-" .. store .. "-" .. mode)
      end
   end

workspace (game.project)
   configurations (configs)
   architecture "x86"
   location "build"
   objdir "build/obj/%{prj.name}/%{cfg.buildcfg}"
   cppdialect "C++latest"
   targetdir "bin/%{cfg.buildcfg}"
   buildoptions { "/dxifcInlineFunctions- /Zc:__cplusplus /utf-8" }
   staticruntime "On"
   multiprocessorcompile ("On")
   startproject (game.project)

   local major = os.date("%d")
   local minor = os.date("%m")
   local build = os.date("%Y")
   local revision = os.date("%H") .. os.date("%M")

   if _OPTIONS["with-version"] then
      local t = {}
      for i in _OPTIONS["with-version"]:gmatch("([^.]+)") do
         t[#t + 1], _ = i:gsub("%D+", "")
      end
      while #t < 4 do t[#t + 1] = 0 end
      major    = math.min(tonumber(t[1]), 255)
      minor    = math.min(tonumber(t[2]), 255)
      build    = math.min(tonumber(t[3]), 65535)
      revision = math.min(tonumber(t[4]), 65535)
   end

   local githash = ""
   local f = io.popen("git rev-parse --short HEAD")
   if f then
      githash = f:read("*a"):gsub("%s+", "")
      f:close()
   end

   local productVersion = major .. "." .. minor .. "." .. build .. "." .. revision
   if githash ~= "" then
      productVersion = productVersion .. "-" .. githash
   end

   filter "configurations:*-Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:*-Release"
      defines { "NDEBUG" }
      optimize "On"
      symbols "On"

   filter {}

project (game.project)
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetextension ".asi"
   characterset ("Unicode")

   defines { "rsc_CompanyName=\"ResidentEvilClassicTrilogy.FusionFix\"" }
   defines { "rsc_LegalCopyright=\"GPL-3.0-or-later\""}
   defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name}\"", "rsc_OriginalFilename=\"%{cfg.buildtarget.name}\"" }
   defines { "rsc_FileDescription=\"ResidentEvilClassicTrilogy.FusionFix\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix\"" }
   defines { "rsc_FileVersion_MAJOR=" .. major }
   defines { "rsc_FileVersion_MINOR=" .. minor }
   defines { "rsc_FileVersion_BUILD=" .. build }
   defines { "rsc_FileVersion_REVISION=" .. revision }
   defines { "rsc_FileVersion=\"" .. major .. "." .. minor .. "." .. build .. "\"" }
   defines { "rsc_ProductVersion=\"" .. productVersion .. "\"" }
   defines { "rsc_GitSHA1=\"" .. githash .. "\"" }
   defines { "rsc_GitSHA1W=L\"" .. githash .. "\"" }
   defines { "_CRT_SECURE_NO_WARNINGS" }

   includedirs { "source" }
   includedirs { "source/includes" }
   files { "source/*.hxx", "source/*.ixx", "source/includes/**.h", "source/includes/**.hpp" }
   files { "source/" .. game.id .. "/**.cpp", "source/" .. game.id .. "/**.ixx", "source/" .. game.id .. "/**.hxx" }
   includedirs { "source/" .. game.id }
   files { "source/resources/Versioninfo.rc" }
   files { "source/resources/MenuText.rc", "source/resources/MenuText.h" }
   resincludedirs { _SCRIPT_DIR }
   files { "data/plugins/" .. game.project .. ".ini", "data/settings/**.ini", "text/**.txt" }

   -- injector
   includedirs { "external/injector/include" }
   includedirs { "external/injector/safetyhook/include" }
   includedirs { "external/injector/zydis" }
   files { "external/injector/safetyhook/include/**.hpp", "external/injector/safetyhook/src/**.cpp" }
   files { "external/injector/zydis/**.h", "external/injector/zydis/**.c" }
   -- hooking
   includedirs { "external/hooking" }
   files { "external/hooking/Hooking.Patterns.h", "external/hooking/Hooking.Patterns.cpp" }
   -- inireader
   includedirs { "external/inireader" }

   links { "user32" }
   files { "games.json", ".env.example", "deploy.ps1", "package.ps1" }

   for _, store in ipairs({ "Steam", "GOG" }) do
      filter ("configurations:" .. game.id .. "-" .. store .. "-*")
         defines { 'FUSIONFIX_TARGET=L"' .. game.id .. ' / ' .. store .. '"' }
         local key = game.id .. "_" .. store:upper()
         local gamepath = envdir(key .. "_DIR")
         if gamepath then
            local executable = envdir(key .. "_EXE") or (store == "Steam"
               and path.join("english", game.languages.english[1]) or game.gogExe)
            debugcommand (path.join(gamepath, executable))
            debugdir (path.getdirectory(path.join(gamepath, executable)))
            if store == "Steam" then
               debugenvs { "SteamAppId=" .. game.appId, "SteamGameId=" .. game.appId }
            end
            postbuildcommands {
               'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. path.getabsolute("deploy.ps1")
                  .. '" -Game ' .. game.id .. ' -Store ' .. store .. ' -GameDirectory "' .. gamepath
                  .. '" -Plugin "$(TargetPath)" -Executable "' .. executable .. '"'
            }
         end
   end
   filter {}
end
