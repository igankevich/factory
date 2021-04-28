(define (my-map proc lst) (map proc lst))
(display (my-map (lambda (x) (+ 1 x)) '(1 2 3)))
(display (if #f 
             (my-map (lambda (x) (+ 1 x)) '(1 2 3)) 
             (my-map (lambda (x) (+ 4 x)) '(4 5 6))))
            ; (define x 2)
(define (my-new-map proc lst) 
    (if (null? lst) '() 
        (cons (proc (car lst)) (my-new-map proc (cdr lst)))))
(display (my-new-map (lambda (x) (+ 7 x)) '(7 8 9)))