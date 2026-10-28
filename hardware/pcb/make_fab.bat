@echo off
rem Regenerates fabrication files for the rev2 board with KiCad 10 kicad-cli.
rem Output: fab\  (Gerbers + Excellon drill + ZIP for the PCB factory), drc_report.txt, renders\
setlocal
cd /d "%~dp0"
set "KCLI=C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
if not exist "%KCLI%" set "KCLI=kicad-cli"
set "PCB=shapdesk_rev2.kicad_pcb"
set "SCH=shapdesk_rev2.kicad_sch"
set "LOG=make_fab.log"
echo === %date% %time% > "%LOG%"
"%KCLI%" version >> "%LOG%" 2>&1
if exist fab rmdir /s /q fab
mkdir fab
mkdir renders 2>nul

echo === ERC >> "%LOG%"
"%KCLI%" sch erc --format report --severity-all --units mm --output erc_report.txt "%SCH%" >> "%LOG%" 2>&1
echo === DRC >> "%LOG%"
"%KCLI%" pcb drc --format report --severity-all --schematic-parity --units mm --output drc_report.txt "%PCB%" >> "%LOG%" 2>&1
echo === SCHEMATIC PDF >> "%LOG%"
"%KCLI%" sch export pdf --output shapdesk_rev2_schematic.pdf "%SCH%" >> "%LOG%" 2>&1

echo === GERBERS >> "%LOG%"
"%KCLI%" pcb export gerbers --output fab\ --layers F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts --subtract-soldermask "%PCB%" >> "%LOG%" 2>&1

echo === DRILL >> "%LOG%"
"%KCLI%" pcb export drill --output fab\ --format excellon --drill-origin absolute --excellon-units mm --excellon-separate-th --generate-map --map-format gerberx2 "%PCB%" >> "%LOG%" 2>&1

echo === SVG >> "%LOG%"
"%KCLI%" pcb export svg --output renders\top_layers.svg --layers F.Cu,F.Mask,F.Silkscreen,Edge.Cuts --mode-single --page-size-mode 2 --exclude-drawing-sheet "%PCB%" >> "%LOG%" 2>&1
"%KCLI%" pcb export svg --output renders\bottom_layers.svg --layers B.Cu,B.Mask,B.Silkscreen,Edge.Cuts --mode-single --page-size-mode 2 --exclude-drawing-sheet --mirror "%PCB%" >> "%LOG%" 2>&1

echo === RENDER >> "%LOG%"
"%KCLI%" pcb render --output renders\render_top.png --side top --width 2400 --height 560 --zoom 3.4 --quality high --background opaque "%PCB%" >> "%LOG%" 2>&1
"%KCLI%" pcb render --output renders\render_bottom.png --side bottom --width 2400 --height 560 --zoom 3.4 --quality high --background opaque "%PCB%" >> "%LOG%" 2>&1

echo === ZIP >> "%LOG%"
powershell -NoProfile -Command "Compress-Archive -Force -Path 'fab\*' -DestinationPath 'shapdesk_rev2_gerbers.zip'" >> "%LOG%" 2>&1
echo === DONE >> "%LOG%"
type "%LOG%"
echo.
echo Done. See make_fab.log and drc_report.txt
pause
