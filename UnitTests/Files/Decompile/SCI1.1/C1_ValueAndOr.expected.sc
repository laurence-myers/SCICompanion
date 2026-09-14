;;; Sierra Script 1.0 - (do not remove this comment)
(script# 908)
(include sci.sh)

(public
	c1 0
)

(procedure (c1 param1 param2 param3 &tmp temp0)
	(= temp0 (or param1 param2))
	(= temp0 (if param1 param2))
	(= temp0 (or param1 (and param2 param3)))
	(= temp0 (if param1 (or param2 param3)))
	(= temp0 (if (and param1 param2) param3))
	(= temp0 (not (and param1 param2)))
	(= temp0 (c1 (or param1 param2) 1 2))
	(if (== (and param1 param2) 5) (= temp0 1))
	(return (and param1 param2))
)
