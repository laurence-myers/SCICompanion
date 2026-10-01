;;; Sierra Script 1.0 - (do not remove this comment)
(script# 968)
(include sci.sh)
(use System)


(instance client_a of Code
	(properties
		name {client}
	)
)

(instance aScript of Script
	(properties)
	
	(method (doit)
		(= client client_a)
	)
)
