class Calculator:
    def __init__(self):
        pass
    def add(self, x, y):
        return x + y
    def subtract(self, x, y):
        return x - y
    def multiply(self, x, y):
        return x * y
    def divide(self, x, y):
        if y == 0:
            return 'Error: Division by zero is not allowed'
        return x / y

calculator = Calculator()
while True:
    print('1. Addition')
    print('2. Subtraction')
    print('3. Multiplication')
    print('4. Division')
    print('5. Exit')
    choice = input('Choose an operation (1/2/3/4/5): ')
    if choice in ('1', '2', '3', '4'):
        num1 = float(input('Enter first number: '))
        num2 = float(input('Enter second number: '))
        if choice == '1':
            print(num1, '+', num2, '=', calculator.add(num1, num2))
        elif choice == '2':
            print(num1, '-', num2, '=', calculator.subtract(num1, num2))
        elif choice == '3':
            print(num1, '*', num2, '=', calculator.multiply(num1, num2))
        elif choice == '4':
            print(num1, '/', num2, '=', calculator.divide(num1, num2))
    elif choice == '5':
        break
    else:
        print('Invalid input')