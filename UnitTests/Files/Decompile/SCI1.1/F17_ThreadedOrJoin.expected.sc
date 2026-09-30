;;; Sierra Script 1.0 - (do not remove this comment)
(script# 939)
(include sci.sh)

(public
	f17ThreadedOr 0
	f17ThreadedOrThree 1
)

(procedure (f17ThreadedOr param1 param2 param3 &tmp temp0)
	(return
		(and
			param1
			(or
				(and param2 (or (and param3 (= temp0 5)) (= temp0 6)))
				1
			)
		)
	)
)

(procedure (f17ThreadedOrThree param1 param2 param3 param4 param5)
	(return (or (and param1 (or param2 param3 param4)) param5))
)
