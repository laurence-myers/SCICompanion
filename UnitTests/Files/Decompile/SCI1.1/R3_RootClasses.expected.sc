;;; Sierra Script 1.0 - (do not remove this comment)
(script# 972)
(include sci.sh)
(use System)


(class Class_972_0
	(properties
		x 5
		y 7
	)
	
	(method (doit)
		(return y)
	)
)

(class r3NamedRoot
	(properties
		name {r3NamedRoot}
		x 0
	)
	
	(method (doit)
		(return x)
	)
)

(class r3NameSecond
	(properties
		x 0
		name {r3NameSecond}
		y 4
	)
	
	(method (doit)
		(return y)
	)
)

(class Class_972_9 of Code
	(properties
		x 3
	)
	
	(method (doit)
		(return x)
	)
)

(class Class_972_4
	(properties
		x {r3Text}
	)
)

(instance r3SecondObj of r3NameSecond
	(properties
		y 9
	)
)

(instance r3Dup_a of r3NameSecond
	(properties
		name {r3Dup}
		y 1
	)
)

(instance r3Dup_b of r3NameSecond
	(properties
		name {r3Dup}
		y 2
	)
)

(class r3Twin of r3NameSecond
	(properties
		x 0
		name {r3Twin}
		y 1
	)
)

(class r3Twin_a of r3NameSecond
	(properties
		x 0
		name {r3Twin}
		y 2
	)
)
