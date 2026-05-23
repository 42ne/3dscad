# Bug Report: Hull завжди повертав порожній результат

**Компонент:** Manifold 3.4.1 — `src/parallel.h`  
**Дата виявлення:** 2026-05-24  
**Severity:** Critical — Hull не працював взагалі  
**Статус:** ✅ Виправлено

---

## Симптом

Вузол `hull()` у дереві сцени давав порожній вьюпорт. Всі три перевантажені
варіанти `Manifold::Hull()` завжди повертали порожній меш:

```
Manifold::Hull(vector<vec3>)      → IsEmpty=1, NumVert=0, NumTri=0
Manifold::Hull()                  → IsEmpty=1
Manifold::Hull(vector<Manifold>)  → IsEmpty=1
```

Навіть для тривіального тетраедра з 4 точок результат був порожнім.

---

## Першопричина

Баг у послідовній реалізації `exclusive_scan` у файлі
`build/manifold-src/src/parallel.h` (рядок ≈ 636):

```cpp
// БУЛО (баг):
T sum = init;
for (; first != last; ++first, ++d_first) {
    *d_first = sum;        // ← спочатку ЗАПИСУЄ в буфер
    sum = f(sum, *first);  // ← потім ЧИТАЄ з того ж буфера — вже зіпсовані дані!
}
```

У `quickhull.cpp::buildMesh()` функція викликається так:

```cpp
exclusive_scan(TransformIterator(counts.begin(), saturate),
               TransformIterator(counts.end(), saturate),
               counts.begin(), 0);  // ← вхід і вихід — той самий масив
```

Вхідний ітератор (`TransformIterator`) і вихідний (`counts.begin()`) вказують на
**той самий масив**. Через аліасинг:

1. Крок 1: `counts[0] = 0` (init) → запис
2. Крок 2: читає `counts[0]` через трансформ-ітератор → отримує `0` (вже зіпсований!)
3. Усі наступні кроки дають `0`

Підсумок: `counts.back() = 0` → вершин після компакції 0 → порожній меш.

---

## Виправлення

**Файл:** `build/manifold-src/src/parallel.h`  
**Патч:** `manifold-hull-fix.patch`

```cpp
// СТАЛО (виправлено):
T sum = init;
for (; first != last; ++first, ++d_first) {
    auto cur = *first;  // читаємо ДО запису — фікс аліасингу
    *d_first = sum;
    sum = f(sum, cur);
}
```

---

## Супутні зміни коду (manifoldcsg.cpp)

| Що змінено | Чому |
|---|---|
| Hull використовує `Manifold::Hull(vector<Manifold>)` замість ручного витягу вершин | Простіше, той самий результат |
| Додано `applyNodeTransform()` до результату Hull | Був відсутній — баг |
| Видалено діагностичний блок і `#include <QDebug>` | Прибирання після debugging |

---

## Відтворення (до виправлення)

```cpp
std::vector<manifold::vec3> tetra = {{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
Manifold h = Manifold::Hull(tetra);
// h.IsEmpty() == true  ← завжди, незалежно від вхідних даних
```

## Результат після виправлення

```
tetra_hull.IsEmpty=0  NumVert=4  NumTri=4   ✓
cubePoints_hull.IsEmpty=0  NumVert=8  NumTri=12  ✓
hull(cyl+cube).IsEmpty=0  NumVert=72  NumTri=140  ✓
```
