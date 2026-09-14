;;; Sierra Script 1.0 - (do not remove this comment)
(script# 918)
(include sci.sh)

; Family 8 (QfG4 hero::useSkill): an indexed += statement, then a comparison
; that a "ret" consumes as a value. Sierra's compiler reuses the index left in
; the accumulator by "lsti" for the following "lati", so the second operand
; has no load of its own. The decompiler must take that index from the load
; before "lsti", not from the += statement, which is the nearest accumulator
; value only by its position in the chunk tree.
; Sierra: (+= [temp0 param1] param2) (return (if (>= [temp0 param1] [temp4 param1]) 1 else 0))
(public
	f8AssignBeforeCondInRet 0
)

(procedure (f8AssignBeforeCondInRet param1 param2 &tmp [temp0 4] [temp4 4])
	(asm
		lap param1
		lsti temp0
		lap param2
		add
		push
		lap param1
		sati temp0
		lap param1
		lsti temp0
		lati temp4
		ge?
		bnt elseBranch
		ldi 1
		jmp join
	elseBranch:
		ldi 0
	join:
		ret
	)
)
