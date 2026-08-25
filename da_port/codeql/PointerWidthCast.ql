/**
 * @name Приведение адреса к указателю другой ширины
 * @description `(lzo_uintp)&u32var` — вызываемый пишет по этому адресу СВОЮ ширину. На x64 запись
 *              восьми байт в четырёхбайтную переменную затирает соседей: стек или конец объекта.
 * @kind problem
 * @problem.severity error
 * @id da-port/pointer-width-cast
 */

import cpp

from Cast cast, PointerType src, PointerType dst, AddressOfExpr addr
where
  cast.getExpr() = addr and
  addr.getType() = src and
  cast.getType() = dst and
  src.getBaseType().getUnspecifiedType() instanceof IntegralType and
  dst.getBaseType().getUnspecifiedType() instanceof IntegralType and
  src.getBaseType().getSize() != dst.getBaseType().getSize()
select cast,
  "Адрес " + src.getBaseType().toString() + " приведён к указателю на " +
  dst.getBaseType().toString() + ": ширина не совпадает."
