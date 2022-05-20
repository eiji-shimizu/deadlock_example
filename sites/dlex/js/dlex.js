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

async function getData(url = '') {
    const response = await fetch(url, {
        method: 'GET',
        mode: 'cors',
        cache: 'no-cache',
        credentials: 'same-origin',
        headers: {
            'Content-Type': 'application/json'
        },
        redirect: 'follow',
        referrerPolicy: 'no-referrer'
    });
    return response.json();
}

const original = {
    orderName: '',
    customerName: '',
    productName: ''
}

function getOrder(e) {
    getData('./getorder')
        .then(data => {
            //alert(data.message);
            document.getElementById('orderdata').innerHTML = '';
            let orders = data.data;
            let contents = '';
            if (orders) {
                for (const element of orders) {
                    contents += '<div class="flex-container">';
                    contents += '<div style="width:100px;" class="t_data">' + element.order_name + '</div>';
                    contents += '<div style="width:200px;" class="t_data">' + element.customer_name + '</div>';
                    contents += '<div style="width:300px;" class="t_data">' + element.product_name + '</div>';
                    contents += '<div style="width:300px;" class="t_data">' + element.datetime + '</div>';
                    contents += '</div>';
                }
            }
            document.getElementById('orderdata').innerHTML = contents;
        });
}

function addOrder(e) {
    const order = JSON.parse(JSON.stringify(original));
    order.orderName = document.getElementById('orderName').value;
    order.customerName = document.getElementById('customerName').value;
    order.productName = document.getElementById('productName').value;
    console.log(order);
    postData('./addorder', order)
        .then(data => {
            //alert(data.message);
        });
}

document.getElementById('getButton').addEventListener('click', getOrder);
document.getElementById('addButton').addEventListener('click', addOrder);