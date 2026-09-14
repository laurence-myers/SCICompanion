;;; Sierra Script 1.0 - (do not remove this comment)
(script# 910)
(include sci.sh)

; Family 3: a three-term or. Each "bt" lands on the join "bnt", as Sierra's
; compiler emits it. Sierra: (if (or temp0 temp1 temp2) (= temp3 1)).
(public
	f3OrThreeTerms 0
)

(procedure (f3OrThreeTerms &tmp temp0 temp1 temp2 temp3)
	(asm
		lat temp0
		bt join
		lat temp1
		bt join
		lat temp2
	join:
		bnt done
		ldi 1
		sat temp3
	done:
		ret
	)
)
