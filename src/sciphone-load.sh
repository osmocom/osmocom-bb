#sudo host/osmocon/osmocon -m mtk -p /dev/ttyUSB0 target/firmware/board/sciphone_dream_g2/blink.mt6235.bin
#BOARDS=sciphone_dream_g2 APPLICATIONS=loader_mtk make
sudo host/osmocon/osmocon -d tr -m mtk -p /dev/ttyUSB0 target/firmware/board/sciphone_dream_g2/$1.mt6235.bin
#sudo host/osmocon/osmocon -m mtk -p /dev/ttyUSB0 target/firmware/board/sciphone_dream_g2/blink.mt6235.bin

