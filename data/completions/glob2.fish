# Generated from Glob2 CLI 2 command definitions.
function __glob2_path
  set -l path ''
  set -l words (commandline -opc)
  if test (count $words) -ge 2; and test "$words[2]" = help
    set -e words[2]
  end
  for word in $words[2..-1]
    switch "$path|$word"
      case '|play'
        set path 'play'
      case '|replay'
        set path 'replay'
      case '|map'
        set path 'map'
      case '|game'
        set path 'game'
      case '|match'
        set path 'match'
      case '|online'
        set path 'online'
      case '|ai'
        set path 'ai'
      case '|script'
        set path 'script'
      case '|assets'
        set path 'assets'
      case '|dev'
        set path 'dev'
      case '|info'
        set path 'info'
      case '|help'
        set path 'help'
      case '|completion'
        set path 'completion'
      case 'ai|check'
        set path 'ai check'
      case 'assets|compose-buildings'
        set path 'assets compose-buildings'
      case 'assets|render-skin'
        set path 'assets render-skin'
      case 'assets|skin-info'
        set path 'assets skin-info'
      case 'dev|random-games'
        set path 'dev random-games'
      case 'dev|stress-maps'
        set path 'dev stress-maps'
      case 'dev|textshots'
        set path 'dev textshots'
      case 'dev|dump-resources'
        set path 'dev dump-resources'
      case 'dev|dump-wheat'
        set path 'dev dump-wheat'
      case 'dev|dump-tiled'
        set path 'dev dump-tiled'
      case 'dev|hive-worker'
        set path 'dev hive-worker'
      case 'game|run'
        set path 'game run'
      case 'game|repeat'
        set path 'game repeat'
      case 'info|version'
        set path 'info version'
      case 'info|sim-version'
        set path 'info sim-version'
      case 'info|catalog'
        set path 'info catalog'
      case 'info|paths'
        set path 'info paths'
      case 'map|generate'
        set path 'map generate'
      case 'map|study'
        set path 'map study'
      case 'map|generators'
        set path 'map generators'
      case 'map|preview'
        set path 'map preview'
      case 'map|render'
        set path 'map render'
      case 'map|import-image'
        set path 'map import-image'
      case 'map|export-image'
        set path 'map export-image'
      case 'map|inspect-package'
        set path 'map inspect-package'
      case 'map|validate-set'
        set path 'map validate-set'
      case 'match|verify'
        set path 'match verify'
      case 'online|join'
        set path 'online join'
      case 'online|play-map'
        set path 'online play-map'
      case 'online|host-map'
        set path 'online host-map'
      case 'online|turn-client'
        set path 'online turn-client'
      case 'script|check'
        set path 'script check'
      case 'script|attach'
        set path 'script attach'
    end
  end
  echo $path
end
function __glob2_at
  set -l current (__glob2_path)
  test "$current" = "$argv[1]"
end
complete -c glob2 -n "__glob2_at ''" -f -a 'play replay map game match online ai script assets dev info help completion'
complete -c glob2 -n "__glob2_at 'ai'" -f -a 'check'
complete -c glob2 -n "__glob2_at 'assets'" -f -a 'compose-buildings render-skin skin-info'
complete -c glob2 -n "__glob2_at 'dev'" -f -a 'random-games stress-maps textshots dump-resources dump-wheat dump-tiled hive-worker'
complete -c glob2 -n "__glob2_at 'game'" -f -a 'run repeat'
complete -c glob2 -n "__glob2_at 'info'" -f -a 'version sim-version catalog paths'
complete -c glob2 -n "__glob2_at 'map'" -f -a 'generate study generators preview render import-image export-image inspect-package validate-set'
complete -c glob2 -n "__glob2_at 'match'" -f -a 'verify'
complete -c glob2 -n "__glob2_at 'online'" -f -a 'join play-map host-map turn-client'
complete -c glob2 -n "__glob2_at 'script'" -f -a 'check attach'
complete -c glob2 -n "__glob2_at 'play'" -l fullscreen
complete -c glob2 -n "__glob2_at 'play'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'play'" -l resizable
complete -c glob2 -n "__glob2_at 'play'" -l no-resizable
complete -c glob2 -n "__glob2_at 'play'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'play'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'play'" -l mute
complete -c glob2 -n "__glob2_at 'play'" -l no-mute
complete -c glob2 -n "__glob2_at 'play'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'play'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'play'" -l window-size -r
complete -c glob2 -n "__glob2_at 'play'" -l username -r
complete -c glob2 -n "__glob2_at 'play'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'play'" -l record -r
complete -c glob2 -n "__glob2_at 'play'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'play'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'play'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'play'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'play'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'play'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'play'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'play'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'play'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'play'" -l help
complete -c glob2 -n "__glob2_at 'replay'" -l fullscreen
complete -c glob2 -n "__glob2_at 'replay'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'replay'" -l resizable
complete -c glob2 -n "__glob2_at 'replay'" -l no-resizable
complete -c glob2 -n "__glob2_at 'replay'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'replay'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'replay'" -l mute
complete -c glob2 -n "__glob2_at 'replay'" -l no-mute
complete -c glob2 -n "__glob2_at 'replay'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'replay'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'replay'" -l window-size -r
complete -c glob2 -n "__glob2_at 'replay'" -l username -r
complete -c glob2 -n "__glob2_at 'replay'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'replay'" -l record -r
complete -c glob2 -n "__glob2_at 'replay'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'replay'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'replay'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'replay'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'replay'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'replay'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'replay'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'replay'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'replay'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'replay'" -l help
complete -c glob2 -n "__glob2_at 'map generate'" -l seed -r
complete -c glob2 -n "__glob2_at 'map generate'" -l set -r
complete -c glob2 -n "__glob2_at 'map generate'" -l width -r
complete -c glob2 -n "__glob2_at 'map generate'" -l height -r
complete -c glob2 -n "__glob2_at 'map generate'" -l teams -r
complete -c glob2 -n "__glob2_at 'map generate'" -l workers -r
complete -c glob2 -n "__glob2_at 'map generate'" -l preview -r
complete -c glob2 -n "__glob2_at 'map generate'" -l preview-size -r
complete -c glob2 -n "__glob2_at 'map generate'" -l preview-scale -r -f -a '2 4 8'
complete -c glob2 -n "__glob2_at 'map generate'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map generate'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map generate'" -l output -r
complete -c glob2 -n "__glob2_at 'map generate'" -l report-file -r
complete -c glob2 -n "__glob2_at 'map generate'" -l config -r
complete -c glob2 -n "__glob2_at 'map generate'" -l map-image -r
complete -c glob2 -n "__glob2_at 'map generate'" -l export-generator-package -r
complete -c glob2 -n "__glob2_at 'map generate'" -l help
complete -c glob2 -n "__glob2_at 'map study'" -l output-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map study'" -l profile -r
complete -c glob2 -n "__glob2_at 'map study'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'map study'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map study'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map study'" -l seed -r
complete -c glob2 -n "__glob2_at 'map study'" -l set -r
complete -c glob2 -n "__glob2_at 'map study'" -l candidates -r
complete -c glob2 -n "__glob2_at 'map study'" -l rotations -r
complete -c glob2 -n "__glob2_at 'map study'" -l write-map
complete -c glob2 -n "__glob2_at 'map study'" -l report -r -f -a 'headroom diagnostics timing terrain'
complete -c glob2 -n "__glob2_at 'map study'" -l perturb -r
complete -c glob2 -n "__glob2_at 'map study'" -l building-artwork -r
complete -c glob2 -n "__glob2_at 'map study'" -l help
complete -c glob2 -n "__glob2_at 'map generators'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'map generators'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map generators'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map generators'" -l help
complete -c glob2 -n "__glob2_at 'map preview'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map preview'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map preview'" -l output -r
complete -c glob2 -n "__glob2_at 'map preview'" -l report-file -r
complete -c glob2 -n "__glob2_at 'map preview'" -l preview -r
complete -c glob2 -n "__glob2_at 'map preview'" -l preview-size -r
complete -c glob2 -n "__glob2_at 'map preview'" -l preview-scale -r -f -a '2 4 8'
complete -c glob2 -n "__glob2_at 'map preview'" -l help
complete -c glob2 -n "__glob2_at 'map render'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map render'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map render'" -l output -r
complete -c glob2 -n "__glob2_at 'map render'" -l render-max-pixels -r
complete -c glob2 -n "__glob2_at 'map render'" -l render-field -r
complete -c glob2 -n "__glob2_at 'map render'" -l field-color -r
complete -c glob2 -n "__glob2_at 'map render'" -l help
complete -c glob2 -n "__glob2_at 'map import-image'" -l seed -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l set -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l width -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l height -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l teams -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l workers -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l preview -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l preview-size -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l preview-scale -r -f -a '2 4 8'
complete -c glob2 -n "__glob2_at 'map import-image'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map import-image'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l output -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l report-file -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l image-seam-width -r
complete -c glob2 -n "__glob2_at 'map import-image'" -l help
complete -c glob2 -n "__glob2_at 'map export-image'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'map export-image'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'map export-image'" -l output -r
complete -c glob2 -n "__glob2_at 'map export-image'" -l help
complete -c glob2 -n "__glob2_at 'map inspect-package'" -l output -r
complete -c glob2 -n "__glob2_at 'map inspect-package'" -l report-file -r
complete -c glob2 -n "__glob2_at 'map inspect-package'" -l help
complete -c glob2 -n "__glob2_at 'map validate-set'" -l report-file -r
complete -c glob2 -n "__glob2_at 'map validate-set'" -l preview -r
complete -c glob2 -n "__glob2_at 'map validate-set'" -l gallery -r -f -a '0 1'
complete -c glob2 -n "__glob2_at 'map validate-set'" -l phase -r
complete -c glob2 -n "__glob2_at 'map validate-set'" -l variation -r
complete -c glob2 -n "__glob2_at 'map validate-set'" -l help
complete -c glob2 -n "__glob2_at 'game run'" -l output-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'game run'" -l profile -r
complete -c glob2 -n "__glob2_at 'game run'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'game run'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'game run'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'game run'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'game run'" -l map-file -r
complete -c glob2 -n "__glob2_at 'game run'" -l load-game -r
complete -c glob2 -n "__glob2_at 'game run'" -l generator -r
complete -c glob2 -n "__glob2_at 'game run'" -l game-seed -r
complete -c glob2 -n "__glob2_at 'game run'" -l map-seed -r
complete -c glob2 -n "__glob2_at 'game run'" -l set -r
complete -c glob2 -n "__glob2_at 'game run'" -l candidates -r
complete -c glob2 -n "__glob2_at 'game run'" -l player -r
complete -c glob2 -n "__glob2_at 'game run'" -l ai-param -r
complete -c glob2 -n "__glob2_at 'game run'" -l ai-script -r
complete -c glob2 -n "__glob2_at 'game run'" -l map-script -r
complete -c glob2 -n "__glob2_at 'game run'" -l alliance -r
complete -c glob2 -n "__glob2_at 'game run'" -l win-condition -r -f -a 'death allies prestige opponents script'
complete -c glob2 -n "__glob2_at 'game run'" -l win-probability -r
complete -c glob2 -n "__glob2_at 'game run'" -l experiment -r
complete -c glob2 -n "__glob2_at 'game run'" -l rule -r
complete -c glob2 -n "__glob2_at 'game run'" -l fork-rule -r
complete -c glob2 -n "__glob2_at 'game run'" -l ticks -r
complete -c glob2 -n "__glob2_at 'game run'" -l gradient-delay -r
complete -c glob2 -n "__glob2_at 'game run'" -l resource-growth-delay -r
complete -c glob2 -n "__glob2_at 'game run'" -l ai-order-delay -r
complete -c glob2 -n "__glob2_at 'game run'" -l save -r
complete -c glob2 -n "__glob2_at 'game run'" -l telemetry -r -f -a 'checksums team-timeline maxima gradient-stats'
complete -c glob2 -n "__glob2_at 'game run'" -l write-replay
complete -c glob2 -n "__glob2_at 'game run'" -l benchmark-warmup -r
complete -c glob2 -n "__glob2_at 'game run'" -l diagnostic-fields -r -f -a 'maxima'
complete -c glob2 -n "__glob2_at 'game run'" -l diagnostic-interval -r
complete -c glob2 -n "__glob2_at 'game run'" -l diagnostic-png
complete -c glob2 -n "__glob2_at 'game run'" -l building-artwork -r
complete -c glob2 -n "__glob2_at 'game run'" -l help
complete -c glob2 -n "__glob2_at 'game repeat'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'game repeat'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'game repeat'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'game repeat'" -l ticks -r
complete -c glob2 -n "__glob2_at 'game repeat'" -l runs -r
complete -c glob2 -n "__glob2_at 'game repeat'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'game repeat'" -l help
complete -c glob2 -n "__glob2_at 'match verify'" -l output-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'match verify'" -l profile -r
complete -c glob2 -n "__glob2_at 'match verify'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'match verify'" -l map-file -r
complete -c glob2 -n "__glob2_at 'match verify'" -l help
complete -c glob2 -n "__glob2_at 'online join'" -l fullscreen
complete -c glob2 -n "__glob2_at 'online join'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'online join'" -l resizable
complete -c glob2 -n "__glob2_at 'online join'" -l no-resizable
complete -c glob2 -n "__glob2_at 'online join'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'online join'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'online join'" -l mute
complete -c glob2 -n "__glob2_at 'online join'" -l no-mute
complete -c glob2 -n "__glob2_at 'online join'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'online join'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'online join'" -l window-size -r
complete -c glob2 -n "__glob2_at 'online join'" -l username -r
complete -c glob2 -n "__glob2_at 'online join'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'online join'" -l record -r
complete -c glob2 -n "__glob2_at 'online join'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'online join'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'online join'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'online join'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'online join'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'online join'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'online join'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'online join'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'online join'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'online join'" -l instance -r
complete -c glob2 -n "__glob2_at 'online join'" -l help
complete -c glob2 -n "__glob2_at 'online play-map'" -l fullscreen
complete -c glob2 -n "__glob2_at 'online play-map'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'online play-map'" -l resizable
complete -c glob2 -n "__glob2_at 'online play-map'" -l no-resizable
complete -c glob2 -n "__glob2_at 'online play-map'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'online play-map'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'online play-map'" -l mute
complete -c glob2 -n "__glob2_at 'online play-map'" -l no-mute
complete -c glob2 -n "__glob2_at 'online play-map'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'online play-map'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'online play-map'" -l window-size -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l username -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'online play-map'" -l record -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'online play-map'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'online play-map'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l instance -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l hash -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l title -r
complete -c glob2 -n "__glob2_at 'online play-map'" -l help
complete -c glob2 -n "__glob2_at 'online host-map'" -l fullscreen
complete -c glob2 -n "__glob2_at 'online host-map'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'online host-map'" -l resizable
complete -c glob2 -n "__glob2_at 'online host-map'" -l no-resizable
complete -c glob2 -n "__glob2_at 'online host-map'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'online host-map'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'online host-map'" -l mute
complete -c glob2 -n "__glob2_at 'online host-map'" -l no-mute
complete -c glob2 -n "__glob2_at 'online host-map'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'online host-map'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'online host-map'" -l window-size -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l username -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'online host-map'" -l record -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'online host-map'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'online host-map'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l instance -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l hash -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l title -r
complete -c glob2 -n "__glob2_at 'online host-map'" -l help
complete -c glob2 -n "__glob2_at 'online turn-client'" -l output-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'online turn-client'" -l profile -r
complete -c glob2 -n "__glob2_at 'online turn-client'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'online turn-client'" -l map-file -r
complete -c glob2 -n "__glob2_at 'online turn-client'" -l orders-per-second -r
complete -c glob2 -n "__glob2_at 'online turn-client'" -l max-seconds -r
complete -c glob2 -n "__glob2_at 'online turn-client'" -l seed -r
complete -c glob2 -n "__glob2_at 'online turn-client'" -l help
complete -c glob2 -n "__glob2_at 'ai check'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'ai check'" -l help
complete -c glob2 -n "__glob2_at 'script check'" -l help
complete -c glob2 -n "__glob2_at 'script attach'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'script attach'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'script attach'" -l help
complete -c glob2 -n "__glob2_at 'assets compose-buildings'" -l base -r
complete -c glob2 -n "__glob2_at 'assets compose-buildings'" -l package -r
complete -c glob2 -n "__glob2_at 'assets compose-buildings'" -l artwork-bundle -r
complete -c glob2 -n "__glob2_at 'assets compose-buildings'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'assets compose-buildings'" -l help
complete -c glob2 -n "__glob2_at 'assets render-skin'" -l manifest -r
complete -c glob2 -n "__glob2_at 'assets render-skin'" -l texture -r
complete -c glob2 -n "__glob2_at 'assets render-skin'" -l material -r
complete -c glob2 -n "__glob2_at 'assets render-skin'" -l output-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'assets render-skin'" -l help
complete -c glob2 -n "__glob2_at 'assets skin-info'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'assets skin-info'" -l help
complete -c glob2 -n "__glob2_at 'dev random-games'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev random-games'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l fullscreen
complete -c glob2 -n "__glob2_at 'dev random-games'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'dev random-games'" -l resizable
complete -c glob2 -n "__glob2_at 'dev random-games'" -l no-resizable
complete -c glob2 -n "__glob2_at 'dev random-games'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'dev random-games'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'dev random-games'" -l mute
complete -c glob2 -n "__glob2_at 'dev random-games'" -l no-mute
complete -c glob2 -n "__glob2_at 'dev random-games'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'dev random-games'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'dev random-games'" -l window-size -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l username -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'dev random-games'" -l record -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'dev random-games'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l display
complete -c glob2 -n "__glob2_at 'dev random-games'" -l runs -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l ticks -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l ai-types -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l map -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l matchup -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l save-game-as -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'dev random-games'" -l help
complete -c glob2 -n "__glob2_at 'dev stress-maps'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev stress-maps'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'dev stress-maps'" -l help
complete -c glob2 -n "__glob2_at 'dev textshots'" -l fullscreen
complete -c glob2 -n "__glob2_at 'dev textshots'" -l no-fullscreen
complete -c glob2 -n "__glob2_at 'dev textshots'" -l resizable
complete -c glob2 -n "__glob2_at 'dev textshots'" -l no-resizable
complete -c glob2 -n "__glob2_at 'dev textshots'" -l custom-cursor
complete -c glob2 -n "__glob2_at 'dev textshots'" -l no-custom-cursor
complete -c glob2 -n "__glob2_at 'dev textshots'" -l mute
complete -c glob2 -n "__glob2_at 'dev textshots'" -l no-mute
complete -c glob2 -n "__glob2_at 'dev textshots'" -l renderer -r -f -a 'gpu software'
complete -c glob2 -n "__glob2_at 'dev textshots'" -l graphics-detail -r -f -a 'full reduced'
complete -c glob2 -n "__glob2_at 'dev textshots'" -l window-size -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l username -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l editor-script -r -f -a 'sgsl usl'
complete -c glob2 -n "__glob2_at 'dev textshots'" -l record -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l videoshot -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l record-fps -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l record-encoder -r -f -a 'auto software'
complete -c glob2 -n "__glob2_at 'dev textshots'" -l record-crf -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l record-chapter-ticks -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev textshots'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l building-catalog -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l compute-threads -r
complete -c glob2 -n "__glob2_at 'dev textshots'" -l output-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev textshots'" -l help
complete -c glob2 -n "__glob2_at 'dev dump-resources'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev dump-resources'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'dev dump-resources'" -l help
complete -c glob2 -n "__glob2_at 'dev dump-wheat'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev dump-wheat'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'dev dump-wheat'" -l team -r
complete -c glob2 -n "__glob2_at 'dev dump-wheat'" -l help
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l repeat-x -r
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l repeat-y -r
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l colonies -r
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l swarms -r
complete -c glob2 -n "__glob2_at 'dev dump-tiled'" -l help
complete -c glob2 -n "__glob2_at 'dev hive-worker'" -l help
complete -c glob2 -n "__glob2_at 'info version'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'info version'" -l help
complete -c glob2 -n "__glob2_at 'info sim-version'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'info sim-version'" -l help
complete -c glob2 -n "__glob2_at 'info catalog'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'info catalog'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'info catalog'" -l generator-package -r
complete -c glob2 -n "__glob2_at 'info catalog'" -l help
complete -c glob2 -n "__glob2_at 'info paths'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'info paths'" -l data-dir -r -f -a '(__fish_complete_directories)'
complete -c glob2 -n "__glob2_at 'info paths'" -l help
complete -c glob2 -n "__glob2_at 'help'" -l format -r -f -a 'text json'
complete -c glob2 -n "__glob2_at 'help'" -l help
complete -c glob2 -n "__glob2_at 'completion'" -l help
