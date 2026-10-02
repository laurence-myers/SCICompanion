;;; Sierra Script 1.0 - (do not remove this comment)
(script# 953)
(include sci.sh)

(public
	v1DupBeforeIf 0
	v1RepeatedTest 1
	v1ReuseAfterTest 2
)

(procedure (v1DupBeforeIf param1 param2)
	(Abs (if param1 (Abs param2) else 7))
)

(procedure (v1RepeatedTest param1 param2 &tmp temp0)
	(if (and param1 (= temp0 param2)) (temp0 init:))
)

(procedure (v1ReuseAfterTest param1)
	(if param1 (Abs param1))
)
