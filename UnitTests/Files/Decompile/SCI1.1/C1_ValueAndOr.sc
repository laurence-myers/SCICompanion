;;; Sierra Script 1.0 - (do not remove this comment)
(script# 908)
(include sci.sh)

; A logical and/or used for its value, not as a condition. Compiled by SCI
; Companion's own compiler. After the value-and/or compiler fix, these leave
; the last evaluated operand in the accumulator (Sierra semantics), so the
; bytecode round-trips.
(public
	c1 0
)

(procedure (c1 a b c &tmp t)
	(= t (or a b))
	(= t (and a b))
	(= t (or a (and b c)))
	(= t (and a (or b c)))
	(return (and a b))
)
