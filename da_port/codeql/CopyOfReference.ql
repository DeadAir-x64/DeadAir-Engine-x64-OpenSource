/**
 * @name Копия вместо ссылки: объект с деструктором копируется из функции, вернувшей ссылку
 * @description `auto x = obj.method()`, где method возвращает ССЫЛКУ на объект с деструктором,
 *              создаёт копию. Копия поверхностная, а её деструктор освобождает те же ресурсы —
 *              то есть рушит живые данные. Так было в gg_distance с графом ИИ.
 * @kind problem
 * @problem.severity error
 * @id da-port/copy-of-reference
 */

import cpp

predicate hasUserDestructor(Class c) {
  exists(Destructor d | d.getDeclaringType() = c and not d.isCompilerGenerated())
}

from LocalVariable v, FunctionCall call, Class ct
where
  // Инициализатор — НЕ сам вызов: для `auto x = f()` там стоит вызов копирующего конструктора, а
  // наш f() лежит внутри. Первый вариант запроса искал вызов напрямую и не нашёл НИЧЕГО, включая
  // заведомо дефектный gg_distance. Ноль находок доказательством не был.
  call = v.getInitializer().getExpr().getAChild*() and
  // ⚠️ Тип копии обязан СОВПАДАТЬ с типом возвращаемой ссылки.
  //
  // Без этого условия запрос ловил любой вызов, возвращающий ссылку, где угодно внутри
  // инициализатора - например `guard(object().sight(), true)`, где sight() просто довод
  // конструктора. Так набралось 47 находок, из которых настоящей была одна.
  call.getTarget().getType().(ReferenceType).getBaseType().getUnspecifiedType() = ct and
  ct = v.getUnspecifiedType() and
  hasUserDestructor(ct) and
  not v.getType() instanceof ReferenceType and
  not v.getType() instanceof PointerType
select v,
  "Копия объекта $@ из функции, вернувшей ссылку: деструктор копии освободит живые ресурсы.",
  ct, ct.getName()
