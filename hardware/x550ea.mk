# ASUS X550EA — эталонная конфигурация железа для XrossOS 0.2
# (варианты X550EA: A4-5000 / реже E1-2100 — параметры ниже для A4-5000,
#  для E1-2100 FPS_BATTERY и плотность волны урезать вдвое в xross.conf)
model   = ASUS X550EA (X550)
cpu     = AMD A4-5000: 4x Jaguar 1.5 GHz, family 16h (btver2), AVX/SSE4.2/AES, 15 W
gpu     = Radeon HD 8330: GCN 1.0 Southern Islands (Kalindi), драйвер amdgpu+radeonsi
screen  = 15.6" 1366x768 LVDS
ram     = 4 GB DDR3L-1600 (1 слот занят, второй часто свободен — апгрейд до 8 GB лучший тюнинг)
storage = 2.5" HDD 500 GB 5400 rpm (замена на SATA SSD — второй лучший тюнинг)
wifi    = MediaTek MT7630e или Atheros AR9485 (зависит от ревизии), Ethernet Realtek
battery = BAT0, адаптер ADP1 (имена в /sys стабильны — оболочка читает их напрямую)
