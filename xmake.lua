add_rules("mode.debug", "mode.release")

option("qjsb_build_examples", function ()
    set_default(false)
    set_showmenu(true)
    set_description("Build QuickJSBinder example programs")
end)

target("QuickJSBinder", function ()
    set_kind("headeronly")
    set_languages("c++20")
    add_includedirs("include", { interface = true })
end)

if has_config("qjsb_build_examples") then

    -- Native plugin shared library (loaded by QuickJS as an ES module via dlopen)
    target("native_plugin", function ()
        set_kind("shared")
        set_languages("c++20")
        add_files("examples/native_scripts/native_plugin.cpp")
        add_deps("QuickJSBinder")
        add_includedirs("deps/include")
        add_linkdirs("deps/lib")
        add_links("qjs")
        -- QuickJS expects the module file next to the importing JS file.
        -- Copy the built .so/.dylib into examples/native_scripts/ with a .so name
        -- (QuickJS uses .so on all POSIX platforms for native module loading).
        after_build(function (target)
            local outfile = target:targetfile()
            local destdir = path.join(os.projectdir(), "examples", "native_scripts")
            os.cp(outfile, path.join(destdir, "native_plugin.so"))
        end)
    end)

    -- Example binaries: every .cpp in examples/
    for _, filepath in ipairs(os.files("examples/*.cpp")) do
        local name = path.basename(filepath)
        -- main.cpp -> QuickJSBinder_example, others keep their filename
        local target_name = (name == "main") and "QuickJSBinder_example" or name

        target(target_name, function ()
            set_kind("binary")
            set_languages("c++20")
            add_files(filepath)
            add_deps("QuickJSBinder")
            add_includedirs("examples")
            set_rundir("$(projectdir)")
            add_includedirs("deps/include")
            add_linkdirs("deps/lib")
            add_links("qjs")
        end)
    end
end
