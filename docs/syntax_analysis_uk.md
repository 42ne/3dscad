# Аналіз синтаксису OpenSCAD — оцінка реалізації в дереві

## Без змін дерева (parse-time / pure evaluation / transpile)

Реалізується в `expression.h` (евалюатор) та/або `openscadparser.cpp` (парсер).
Не потребує нових `Operation`, `ShapeNode` або `TreeNode::Type`.

| Синтаксис | Де реалізувати | Примітка |
|---|---|---|
| **`assign()` inline** | expression.h | Те саме що `let`, тільки в `()` а не `{}`. На рівні парсингу виразу |
| **`function f(x)=expr;`** | expression.h + parser | Зберегти в `QHash<QString, FunctionDef>`, викликати при eval. Не створює геометрії |
| **Block коментарі `/* */`** | lexer / parser | Чисто токенізація, дерево не змінюється |
| **`$fn`, `$fa`, `$fs`** | expression.h | Додати як спеціальні змінні з дефолтами; для preview повертати велике `$fn` |
| **`echo()`** | parser | Друкувати або ігнорувати; додати в `startsWithKnownKeyword` |
| **`assert()`** | parser | Аналогічно `echo`, помилка якщо умова false |
| **`undef`** | expression.h | Обробляти як `NaN` або 0 в арифметиці |
| **`rands()`** | expression.h | Одне випадкове значення для preview |
| **`norm()`, `cross()`** | expression.h | Векторні функції — додати в маппінг |
| **`len()`** | expression.h | `len([1,2,3]) = 3`, `len("abc") = 3` |
| **`concat()`, `lookup()`** | expression.h | Обмежено без list comprehensions |
| **`str()`** | expression.h | Конкатенація чисел/рядків |

## Потребують змін дерева (нові `Operation` / `ShapeNode` / архітектурні зміни)

### Нові операції (TreeNode::Operation)

| Синтаксис | Потрібен | Чому |
|---|---|---|
| **`resize([x,y,z])`** | `Operation::Resize` | Розтягує AABB до цільового розміру. Можна зімітувати Scale з обчисленими факторами, але неточно |
| **`offset()`** | `Operation::Offset` | 2D трансформація контуру; потребує geometry processing |
| **`projection()`** | `Operation::Projection` | 3D→2D, потребує рендеру |
| **`rotate_extrude()`** | `Operation::RotateExtrude` | Інший тип екструзії, відмінний від `linear_extrude` |
| **`render()`** (convexity) | `Operation::Render` | Зараз флатенується; щоб зберігати `convexity` треба ноду |

### Нові примітиви (ShapeNode::Type)

| Синтаксис | Потрібен | Чому |
|---|---|---|
| **`text()`** | `ShapeNode::Text` | Бібліотека шрифтів + генерація mesh |
| **`import()`** | `ShapeNode::ImportedMesh` | Файловий ввід/вивід, зберігання зовнішньої mesh |
| **`surface()`** | `ShapeNode::Surface` | Зображення → heightmap → mesh |

### Архітектурні зміни

| Синтаксис | Проблема |
|---|---|
| **`children()`** | Модуль повинен мати доступ до своїх дітей в момент рендеру. Зміна всієї моделі рендеру |
| **List comprehensions** | Можуть генерувати набір нод (як `for`). Або ітерувати в парсері, або додати `Operation::ListComprehension` |
| **`use <file>`** | Багатофайловість: окремий парсинг + merge дерева |
| **`include <file>`** | Те саме + прямий імпорт змінних |
| **Вкладені модулі** | Ієрархія module-в-module, зміна резолюції `ModuleCall` |
| **`square()` з `center`** | Вже частково (Z=0.1), але `center` ігнорується; можна додати без tree |

### Розширення параметрів існуючих операцій

| Синтаксис | Зміна |
|---|---|
| **`linear_extrude`** `twist`, `slices`, `scale`, `center` | Парсити параметри й передавати в рендер `LinearExtrudeOperation` |
| **`color()`** з альфою / рядками імен | Розширити парсер; без зміни типів нод |
| **`polyhedron()`** з виразами в `points`/`faces` | Парсер зараз читає тільки літерали |
| **`cube(size, center=true)`** | `center` ігнорується; можна додати параметр або продовжити ігнорувати |

## Прогалини round-trip / збереження синтаксису

Це потрібно для повної сумісності OpenSCAD у сценарії:
`OpenSCAD source -> tree -> generated OpenSCAD` без втрати непідтриманого синтаксису.
Це окремо від геометричної підтримки: конструкція може бути read-only у UI, але все одно має зберігатися.

| Синтаксис / конструкція | Потрібна зміна | Чому |
|---|---|---|
| **Невідомі statements / непідтримані calls** | Додати `RawStatement`, `RawBlock` або `UnsupportedCall` вузли дерева | Поточний parser може пропускати або flatten-ити невідомий код, тому generator не може відновити його |
| **Коментарі й whitespace trivia** | Зберігати leading/trailing comments і форматування як metadata або raw-фрагменти біля вузлів | Потрібно для lossless round-trip і читабельного згенерованого коду |
| **Debug/background/root/disable modifiers** `#`, `%`, `!`, `*` | Зберігати modifier metadata на вузлах або весь raw statement | Вони змінюють preview/debug semantics і не мають зникати мовчки |
| **Директиви `include <file>` / `use <file>`** | Додати directive nodes або raw-preserved top-level записи | Реальні проєкти залежать від зовнішніх модулів/функцій; втрата директив ламає згенерований код |
| **Function/module declarations, які дерево не редагує** | Зберігати оригінальний source декларації, якщо повного AST-вузла ще немає | Щоб nested modules/functions або незвичні signatures не випадали з коду |
| **Expression text, який не вдалося обчислити** | Зберігати оригінальні expression strings навіть при помилці eval | UI може показати unresolved value, але codegen не має підміняти це fallback-числом |
| **Порядок globals/modules/body/calls** | Зберігати початковий top-level order або order groups | Поточний generator нормалізує категорії, що змінює layout і іноді semantics |
| **Source ranges для непідтриманого синтаксису** | Розширити source map на raw/preserved вузли | Потрібно для selection, diagnostics і майбутнього безпечного редагування |

### Набір round-trip тестів

Додати тести для:

- коментарів перед/всередині/після блоків;
- modifiers `#`, `%`, `!`, `*` на primitives і groups;
- unknown wrapper з відомими children;
- `include` / `use`;
- function declaration + function call у параметрах;
- nested module declaration;
- unsupported expression, який має лишитися текстом;
- `polygon(paths=...)` і `polyhedron(..., convexity=...)`;
- import/export ordering: variables, modules, body statements і calls.

## Пріоритетна матриця (без tree)

| Функція | Частота | Складність | Користь |
|---|---|---|---|
| `function` | Дуже висока | Середня | Відкриває параметричний дизайн |
| Block коментарі | Висока | Низька | Прибирає поширену проблему |
| `$fn` | Висока | Низька | Краща якість preview |
| `echo` | Середня | Низька | Відлагодження |
| `len` | Середня | Низька | Базова інтроспекція векторів |
| `undef` | Середня | Низька | Ближче до реального OpenSCAD |
| `norm` / `cross` | Низька | Низька | Векторна математика |
| `rands` | Низька | Низька | Рандомізація |
| `str` / `concat` | Низька | Середня | Рядкові операції |

## Пріоритетна матриця (зі зміною tree)

| Функція | Частота | Складність | Користь |
|---|---|---|---|
| `rotate_extrude` | Висока | Середня | Токарні форми |
| `resize` | Середня | Низька | Практичний трансформ |
| `text` | Середня | Висока | Маркування/гравіювання |
| `linear_extrude` параметри | Середня | Середня | Екструзії з крутінням |
| `children()` | Середня | Дуже висока | Перевикористовувані модулі |
| `use` / `include` | Висока | Висока | Багатофайлові проєкти |
| `import` | Середня | Середня | Зовнішні STL |
