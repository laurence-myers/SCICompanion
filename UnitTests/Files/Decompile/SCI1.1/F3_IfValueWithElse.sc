;;; Sierra Script 1.0 - (do not remove this comment)
(script# 913)
(include sci.sh)

; Family 3: an if with an else used as a value. Sierra: (= temp1 (if temp0 1 else 2)).
(public
	f3IfValueWithElse 0
)

(procedure (f3IfValueWithElse &tmp temp0 temp1)
	(asm
		lat temp0
		bnt elseBranch
		ldi 1
		jmp join
	elseBranch:
		ldi 2
	join:
		sat temp1
		ret
	)
)
