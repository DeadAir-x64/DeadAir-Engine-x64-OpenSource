# Лицензии и уведомления

Этот репозиторий — форк [OpenXRay](https://github.com/OpenXRay/xray-16) с портом мода **Dead Air**
на 64-битный движок. Здесь собрано, что кому принадлежит и на каких условиях используется.

---

## 1. Движок X-Ray — GSC Game World

Основа — исходники движка **X-Ray**, разработанного GSC Game World. OpenXRay и, следовательно, этот
порт являются **производной работой** и не являются официальным продуктом GSC.

При использовании движка для модификаций к играм серии S.T.A.L.K.E.R. обязательны к соблюдению:

- [End User License Agreement](https://www.gsc-game.com/eula/)
- [Fan Content Creation Guidelines](https://www.gsc-game.com/guidelines/)

Полный текст — в [License.txt](License.txt).

## 2. OpenXRay — MIT

Правки и новый код команды OpenXRay распространяются по лицензии MIT. Текст — там же,
в [License.txt](License.txt). Мы её не меняем и не переопределяем.

## 3. Правки этого порта — MIT

Всё, что добавлено в этом форке и помечено в исходниках маркером `[DA_PORT]`, распространяется на тех
же условиях, что и код OpenXRay, — **MIT**. Иначе и нельзя: это правки внутри их дерева.

```
Copyright (c) 2026 DanesCrai1 and Dead Air x64 port contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## 4. Контент мода Dead Air — автор мода

В `da_port/gamedata/` лежат конфиги, скрипты и шейдеры мода **Dead Air** с внесёнными в них правками
порта. Это контент автора мода, а не наш.

**Опубликовано с разрешения автора оригинала.** Ассеты — модели, текстуры, звуки, уровни и архивы
`database/` — в репозиторий **не входят** и здесь не распространяются.

## 5. Сторонние SDK

В репозиторий не входят: у каждого своя лицензия, и брать их надо у правообладателя.

### NVIDIA DLSS

```bash
git clone --depth 1 https://github.com/NVIDIA/DLSS.git Externals/nvngx
```

Лицензия разрешает распространять `nvngx_dlss.dll` вместе с приложением, но **запрещает изменять
библиотеку**. Она к тому же подписана, и NGX проверяет подпись при загрузке — так что технически это
тоже невозможно. Текст лицензии обязан ехать вместе со сборкой:
[da_port/NVIDIA_DLSS_LICENSE.txt](da_port/NVIDIA_DLSS_LICENSE.txt).

Прослойка `Externals/nvngx_shim` — **наша** и входит в репозиторий. Она существует потому, что NVIDIA
отдаёт NGX статическими библиотеками MSVC, а порт собирается MinGW: напрямую они не линкуются.

### Intel XeSS

```bash
git clone --depth 1 https://github.com/intel/xess.git Externals/xess
```

Лицензия Intel требует, чтобы её текст и `third-party-programs.txt` распространялись вместе со
сборкой.

### AMD FidelityFX Super Resolution

FSR 2 и FSR 3 собираются из исходников в `Externals/ffx-fsr2-api` и `Externals/ffx-fsr3`.
Распространяются AMD по лицензии MIT.

---

## Коротко

| Что | Чьё | На каких условиях |
|---|---|---|
| Движок X-Ray | GSC Game World | EULA + правила по фан-контенту |
| Правки OpenXRay | команда OpenXRay | MIT |
| Правки порта `[DA_PORT]` | этот проект | MIT |
| Конфиги и скрипты Dead Air | автор мода | с разрешения автора |
| Ассеты Dead Air | автор мода | **не распространяются здесь** |
| DLSS, XeSS, FSR | NVIDIA, Intel, AMD | свои лицензии, в дерево не входят |
