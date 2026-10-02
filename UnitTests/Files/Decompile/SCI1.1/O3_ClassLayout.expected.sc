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

(class o3Dup of o3Base
	(properties &layout
		name {o3Dup}
		y 1
	)
	
	(method (doit)
		(return y)
	)
)

(class o3Dup_a of o3Base
	(properties &layout
		name {o3Dup}
		x 1
	)
	
	(method (doit)
		(return x)
	)
)

(class a_string of o3Base
	(properties &layout
		y {a string}
		x 0
	)
	
	(method (doit)
		(return x)
	)
)

(class Class_974_8 of o3Base
	(properties &layout)
	
	(method (doit)
		(return 0)
	)
)

(class o3EmptySub of Class_974_8
	(properties
		name {o3EmptySub}
		client 1
	)
	
	(method (doit)
		(return client)
	)
)

(class Class_974_10
	(properties &layout)
	
	(method (doit)
		(return 1)
	)
)
