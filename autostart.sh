#!/bin/env bash
setxkbmap us -option caps:escape
xset r rate 200 50
polybar main &
exec feh --bg-scale ~/desktop.*
