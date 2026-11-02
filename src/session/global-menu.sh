#!/usr/bin/env bash

menu_module="$library_dir/gtk-3.0/libmavernix-global-menu.so"
if [[ ${MAVERNIX_GLOBAL_MENU:-1} != 0 && -f $menu_module ]]; then
    case ":${GTK3_MODULES:-}:${GTK_MODULES:-}:" in
        *appmenu-gtk-module*|*unity-gtk-module*) ;;
        *":$menu_module:"*) ;;
        *) export GTK3_MODULES="${GTK3_MODULES:+$GTK3_MODULES:}$menu_module" ;;
    esac
fi
unset menu_module
