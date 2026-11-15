# Liquid Glass — Нативный плагин Hyprland (OpenGL ES 3.2)

Чистая (clean-room, MIT) реализация оптического преломляющего стекла для **Hyprland 0.57.2** без форков композитора: плагин перехватывает `Render::GL::createBlurProvider` через официальный `HyprlandAPI`, оборачивает штатный провайдер размытия в `CLiquidGlassBlurProvider` и гоняет те же самые шейдеры из [../shaders](../shaders), транслированные в `#version 320 es`.

---

## 1. Архитектура

```text
Hyprland CHyprGLRenderer::blurFramebuffer(ctx, source, strength, originalDamage, context)
  │
  ├── [!enabled || !context.shape] ──► штатный m_fallback (Kawase / Acrylic / …)
  │
  └── [есть SBlurShape (окно / слой / виджет)]
        │
        ▼
      CLiquidGlassPipeline::renderPanel(...)   (OpenGL ES 3.2, CGLStateGuard)
        ├── 1. copy_prepare (ES 3.20): RGBX alpha=1 + sRGB→Linear → mip 0 (RGBA16F)
        ├── 2. downsample_clamped × 4: 13-tap + uvClampRect     → mip 1..4
        └── 3. liquid_glass_panel: G2-сквиркл SDF + 3-зонное преломление
                                 + 7-tap спектральная дисперсия
                                 + Rec.709 адаптивный блик + Linear→sRGB → outFB
```

* **Единый источник истины:** [../scripts/gen_plugin_shaders.py](../scripts/gen_plugin_shaders.py) на этапе сборки генерирует `plugin/build/generated_shaders.hpp` напрямую из `shaders/include/*.glsl`, `shaders/glass_quad.vert`, `shaders/downsample_clamped.frag` и `shaders/liquid_glass_panel.frag`. Никакой дублированной шейдерной математики.
* **Безопасность GL-состояния:** `CGLStateGuard` сохраняет и восстанавливает FBO (draw/read), viewport, scissor, VAO, UBO binding 0, active texture + units 0..2 и флаги `BLEND`/`CULL`/`DEPTH`/`STENCIL`.
* **Автоподстройка геометрии:** по умолчанию (`squircle_smoothness = -1`) плагин выводит суперэллипс-сглаживание из поля `roundingPower` конкретного окна в Hyprland, чтобы блики на фаске не срезались округлыми углами окна.

---

## 2. Сборка и автономная GPU-проверка

Требования (Arch / Linux): `hyprland` (заголовки в `/usr/include/hyprland`), `pixman`, `libdrm`, `libglvnd` (EGL/GLESv2), `g++` (тем же компилятором/ABI, что и сам `Hyprland`), `python3`.

```bash
# Из корня репозитория:
make plugin        # соберет plugin/build/liquid-glass.so
make test-plugin   # соберет и прогонит plugin/build/gles_plugin_test на реальном GPU (EGL ES 3.2)
```

---

## 3. Загрузка и настройка

### Быстрый тест в живой сессии Hyprland

```bash
hyprctl plugin load /home/eegor/.gemini/antigravity/scratch/liquid-glass-cleanroom/plugin/build/liquid-glass.so
hyprctl liquid_glass:status

# Живая подстройка в Hyprland 0.57+ с Lua-конфигом (hyprctl eval):
hyprctl eval 'hl.config({ plugin = { liquid_glass = { refraction = 24, dispersion = 4.0, tint_alpha = 0.14 } } })'
# Проверка значения опции:
hyprctl getoption plugin:liquid_glass:refraction -j

# (Для классического hyprland.conf на Hyprlang: `hyprctl keyword plugin:liquid_glass:refraction 24`)

# Выгрузка (с освобождением GL-ресурсов и возвратом штатного провайдера):
hyprctl plugin unload /home/eegor/.gemini/antigravity/scratch/liquid-glass-cleanroom/plugin/build/liquid-glass.so
```

### Постоянный конфиг для Hyprland Lua (`hyprland.lua`)

```lua
hl.exec_once("hyprctl plugin load /abs/path/to/liquid-glass-cleanroom/plugin/build/liquid-glass.so")

hl.config({
  decoration = {
    rounding = 14,
    blur = { enabled = true },
  },
  plugin = {
    liquid_glass = {
      enabled = true,
      refraction = 22.0,
      bevel_width = 24.0,
      bevel_offset = 0.0,
      dispersion = 3.5,
      dispersion_curve = 1.35,
      frost_lod = 2.3,
      edge_lod = 0.35,
      specular = 0.55,
      spec_width = 2.5,
      rim_glow = 0.18,
      luma_adapt = 1.0,
      squircle_smoothness = -1.0, -- авто под decoration.rounding_power
      vibrancy = 0.22,
      brightness = 1.02,
      contrast = 1.08,
      tint_alpha = 0.14,
      tint_color = "0xFFEAF2FF",
      damage_pad = 28.0,
    },
  },
})
```

---

## 4. Все параметры (`plugin:liquid_glass:*`)

| Ключ | Диапазон | По умолчанию | Описание |
|---|---|---|---|
| `enabled` | `bool` | `true` | Включить эффект стекла (при `false` работает нативный фолбэк Hyprland) |
| `refraction` | `0..128` | `22.0` | Смещение преломления по фаске (px) |
| `bevel_width` | `1..128` | `24.0` | Ширина оптической фаски (px) |
| `bevel_offset` | `-32..64` | `0.0` | Сдвиг границы фаски внутрь/наружу (px) |
| `dispersion` | `0..32` | `3.5` | Спектральное расщепление R/G/B по фаске (px) |
| `dispersion_curve` | `0.25..4` | `1.35` | Степень нелинейности кривой дисперсии |
| `frost_lod` | `0..4` | `2.3` | Уровень мип-пирамиды в матовом центре |
| `edge_lod` | `0..4` | `0.35` | Четкость преломленной кромки |
| `specular` | `0..3` | `0.55` | Сила направленного блика |
| `spec_width` | `0.5..32` | `2.5` | Толщина бликовой полосы (px) |
| `rim_glow` | `0..2` | `0.18` | Косинусное свечение кромки |
| `luma_adapt` | `0..1` | `1.0` | Ослабление блика на ярком фоне |
| `squircle_smoothness` | `-1..1` | `-1.0` | Гладкость G2-углов (`< 0` = авто из Hyprland `roundingPower`) |
| `vibrancy` | `-1..2` | `0.22` | Добавочная насыщенность внутри стекла |
| `brightness` | `0.1..3` | `1.02` | Линейный множитель яркости |
| `contrast` | `0.2..3` | `1.08` | Контраст относительно перцептивного серого (`0.214`) |
| `tint_alpha` | `0..0.9` | `0.14` | Плотность тонировки |
| `tint_color` | `color` | `0xFFEAF2FF` | Цвет тонировки (`0xAARRGGBB`) |
| `damage_pad` | `0..128` | `28.0` | Запас расширения `damage` для преломляющей кромки (px) |
