;;; Sierra Script 1.0 - (do not remove this comment)
(script# 929)
(include sci.sh)

; A dead branch: Sierra's compiler emits a bnt right after a bnt to the
; same target (QfG4 castAreaScript::changeState and a dozen more, all of
; the shape (and (> (x view:) A) (< (x view:) B))). The accumulator is
; unchanged at the second bnt, so it is never taken. Left in, the chunk
; stage cloned the compare for it and printed the operand twice.
; Sierra: (if (and (> param1 17) (< param1 21)) (= temp0 1) else (= temp0 2))
(public
	b1DeadBranch 0
)

(procedure (b1DeadBranch param1 &tmp temp0)
	(asm
		lsp param1
		ldi 17
		gt?
		bnt elseBranch
		lsp param1
		ldi 21
		lt?
		bnt elseBranch
		bnt elseBranch
		ldi 1
		sat temp0
		jmp done
	elseBranch:
		ldi 2
		sat temp0
	done:
		ret
	)
)
