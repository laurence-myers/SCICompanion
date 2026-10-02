;;; Sierra Script 1.0 - (do not remove this comment)
(script# 973)
(include sci.sh)
(use System)


(class o2Same of Code
	(properties
		x 1
	)
)

(class o2Same_a of Code
	(properties
		name {o2Same}
		y 2
	)
)

(instance o2OfFirst of o2Same
	(properties)
)

(instance o2OfSecond of o2Same_a
	(properties)
)

(instance Script_a of Code
	(properties
		name {Script}
	)
)

(instance o2Script of Script
	(properties)
	
	(method (doit)
		(o2OfFirst doit:)
		(o2OfSecond doit:)
		(Script_a doit:)
	)
)
