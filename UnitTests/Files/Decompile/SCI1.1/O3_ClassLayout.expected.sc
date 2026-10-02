;;; Sierra Script 1.0 - (do not remove this comment)
(script# 974)
(include sci.sh)
(use System)


(class o3Base of Code
	(properties
		x 0
		y 0
	)
)

(class o3Layout of o3Base
	(properties &layout
		name {o3Layout}
		client 0
		y 5
		cycles 0
	)
	
	(method (doit)
		(if cycles (= y client))
	)
)

(class Class_974_2 of o3Base
	(properties &layout
		y 2
		x 1
	)
	
	(method (doit)
		(return x)
	)
)

(class o3Sub of o3Layout
	(properties
		client 0
		y 5
		cycles 0
		x 9
	)
	
	(method (doit)
		(return (+ x y))
	)
)

(instance o3Inst of o3Layout
	(properties
		cycles 3
	)
)
