// POST メソッドの実装の例
async function postData(url = '', data = {}) {
    const response = await fetch(url, {
        method: 'POST',
        mode: 'cors',
        cache: 'no-cache',
        credentials: 'same-origin',
        headers: {
            'Content-Type': 'application/json'
        },
        redirect: 'follow',
        referrerPolicy: 'no-referrer',
        body: JSON.stringify(data)
    });
    return response.json();
}

const original = {
    orderName: '',
    customerName: '',
    productName: ''
}

function getOrder() {
}

function addOrder(e) {
    const order = JSON.parse(JSON.stringify(original));
    order.orderName = document.getElementById('orderName').value;
    order.customerName = document.getElementById('customerName').value;
    order.productName = document.getElementById('productName').value;
    console.log(order);
    postData('./addorder', order)
        .then(data => {
            console.log(data);
        });
}

document.getElementById('addButton').addEventListener('click', addOrder);